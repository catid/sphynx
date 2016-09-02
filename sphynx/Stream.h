#pragma once

#include <stdint.h>

#include "Tools.h"

#include <type_traits> // std::is_enum() std::enable_if()
#include <string>
#include <memory>
#include <vector>

//-----------------------------------------------------------------------------
// Stream
//
// Wraps a fixed-length buffer for reading/writing.
class Stream
{
public:
    static const int MaxFieldSize = 16; // bytes

    Stream();
    ~Stream();

    // No copies, please.
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;

    // Clear out any written data and set the write pointer back to the front
    void WriteReset();

    // Wrap a buffer for read or write.
    void WrapWrite(void* buffer, size_t size)
    {
        WrapBuffer(buffer, static_cast<int>( size ), true);
    }
    void WrapRead(const void* buffer, size_t size)
    {
        WrapBuffer((void*)buffer, static_cast<int>( size ), false);
    }

    void WrapBuffer(void* buffer, int size, bool writing);

    // Getters
    bool IsWriting() const { return Writing; }
    bool Good() const { return !Truncated; }
    int GetUsed() const { return Used; }
    int GetBufferSize() const { return Size; }
    int GetRemaining() const { return Size - Used; }
    u8* GetFront() const { return Front; }
    bool IsDynamic() const { return Dynamic != nullptr; }

    bool UsedWholeBuffer() const { return Size == Used; }

    // Setters
    void Truncate() { Truncated = true; } // Mark as truncated

    // Get a block of memory for manual data serialization.  Read or write to the block.
    // Returns nullptr on truncation.
    u8* GetBlock(int bytes);

    // Serialize a vector of types.
    // Has to be defined before the catchall serializer.
    template<typename T>
    inline bool Serialize(std::vector<T>& vec)
    {
        static_assert(std::is_trivially_copyable<T>::value, "Serialize can only take vectors of trivially copyable types.");
        return SerializeVec(vec);
    }
    template<typename T>
    inline bool Serialize(const std::vector<T>& vec)
    {
        static_assert(std::is_trivially_copyable<T>::value, "Serialize can only take vectors of trivially copyable types.");
        return SerializeVec(vec);
    }

    // Looks like a vector, none of the safety or memory management. Does not own the buffer it is pointed at.
    template<typename T>
    struct vector_view {
    public:
        static_assert(std::is_trivially_copyable<T>::value, "vector_view can only be used with trivially copyable types.");
        vector_view() : buffer(nullptr), count(0) {}
        vector_view(T* buffer, size_t count) : buffer(buffer), count(count) {}

        T* data() const
        {
            return buffer;
        }

        size_t size() const
        {
            return count;
        }

        T* buffer;
        size_t count;
    };

    template<typename T>
    inline bool Serialize(vector_view<T>& vec)
    {
        return SerializeVecView(vec);
    }

    template<typename T>
    inline bool Serialize(const vector_view<T>& vec)
    {
        return SerializeVecView(vec);
    }

    // Serialize a plain-old data type (everything goes through here)
    // Return false on truncation or other errors.
    template<typename T> inline bool Serialize(T& var);
    template<typename T> inline bool Serialize(const T& var);

    // Serialize one type as another via temp casting.
    template<typename RealType, typename SerializeType>
        inline bool SerializeAs(RealType& var);

    // Serialize one type as another via temp casting (const version).
    template<typename RealType, typename SerializeType>
        inline bool SerializeAs(const RealType& var);

private:
    // Writing = true: Serialize() will write to the buffer and fail when it runs out of space.
    // Writing = false: Serialize() will read from the buffer and fail on truncation.
    bool    Writing;    // Is writing to the buffer?
    u8*     Front;      // Pointer to front of the wrapped buffer
    int     Size;       // Size of the buffer
    int     Used;       // Number of bytes used
    bool    Truncated;  // Set if truncated

    // Dynamic buffer implementation:
    u8* Dynamic;        // Buffer allocated dynamically to store data

    // Grow the buffer dynamically. Returns false on any failure.
    // This will always fail during reading.  During writing, it may allocate a dynamic buffer.
    bool Grow(int newUsed);

    // Internal vector serializing methods.
    template<typename T>
    bool SerializeVec(std::vector<T>& vec);
    template<typename T>
    bool SerializeVec(const std::vector<T>& vec);

    template<typename T>
    inline bool SerializeVecView(vector_view<T>& vec);
    template<typename T>
    inline bool SerializeVecView(const vector_view<T>& vec);
};

// Define any 32-bit/64-bit serialization fix-ups here:
// These will serialize *sometimes smaller* types as larger types to make them
// work cross-process.
#define OVR_SERIALIZE_OVERRIDE(RealType, SerializeType) \
    template<> inline bool Stream::Serialize(RealType& var) { return SerializeAs<RealType, SerializeType>(var); }

OVR_SERIALIZE_OVERRIDE(void*, uint64_t);

// Add more here as needed...

// Boolean serialization
template<> inline bool Stream::Serialize(bool& var);
template<> inline bool Stream::Serialize(const bool& var);

// String serialization
template<> inline bool Stream::Serialize(std::string& var);
template<> inline bool Stream::Serialize(const std::string& var);
template<> inline bool Stream::Serialize(const char*& var);


//-----------------------------------------------------------------------------
// Serialization helpers

template<typename T>
inline bool Stream::Serialize(T& var)
{
    // Please read:
    // If you get these static asserts it means that the data you are trying to serialize is too complex.
    // Instead, you should write a Serialize() function that serializes each class member.

    static_assert(std::is_integral<T>::value || std::is_floating_point<T>::value,
                  "Variables must be an integral/fp type. Please define a serialization override for template type T.");
    static_assert(sizeof(T) <= MaxFieldSize, "Variable is too long.");

    // The big danger for serialization is that 32-bit and 64-bit apps will have different memory layout
    // and variable sizes for structures in C++.  By serializing each member individually, overrides are
    // able to serialize the members in a way that's independent of whether the app is 64-bit or not.
    // Please also see OVR_SERIALIZE_OVERRIDE() at the end of this file.

    if (Truncated)
        return false;

    int newUsed = Used + (int)sizeof(T);

    // If truncated,
    if (newUsed > Size && !Grow(newUsed))
    {
        Truncated = true;
        return false;
    }

#ifdef ANDROID
    u8* ptr = Front + newUsed - sizeof(T);
    if (Writing)
        memcpy(ptr, &var, sizeof(T));
    else
        memcpy(&var, ptr, sizeof(T));
#else
    T* ptr = reinterpret_cast<T*>(Front + newUsed - sizeof(T));
    if (Writing)
        *ptr = var;
    else
        var = *ptr;
#endif

    // Increment offset
    Used = newUsed;
    return true;
}

// Serialize a plain-old data type (everything goes through here)
// Return false on truncation or other errors.
template<typename T>
inline bool Stream::Serialize(const T& var)
{
    if (!IsWriting())
    {
        Truncate();
        return false;
    }

    T temp = var;
    return Serialize(temp);
}

// Serialize one type as another via temp casting.
template<typename RealType, typename SerializeType>
inline bool Stream::SerializeAs(RealType& var)
{
    static_assert(sizeof(RealType) <= sizeof(SerializeType), "Misused");

    if (IsWriting())
    {
        SerializeType temp = (SerializeType)(var);
        if (!Serialize(temp))
        {
            Truncate(); // In case override forgets to do this.
            return false;
        }
    }
    else
    {
        if (std::is_const<RealType>::value)
        {
            DEBUG_BREAK; // Cannot write to a const output member.
            return false;
        }

        SerializeType temp;
        if (!Serialize(temp))
        {
            Truncate(); // In case override forgets to do this.
            return false;
        }
        var = (RealType)(temp);
    }

    return true;
}

// Serialize one type as another via temp casting (const version).
template<typename RealType, typename SerializeType>
inline bool Stream::SerializeAs(const RealType& var)
{
    static_assert(sizeof(RealType) <= sizeof(SerializeType), "Misused");

    if (!IsWriting())
    {
        Truncate();
        return false;
    }

    SerializeType temp = (SerializeType)(var);
    if (!Serialize(temp))
    {
        Truncate(); // In case override forgets to do this.
        return false;
    }

    return true;
}


//-----------------------------------------------------------------------------
// Common serialization functions

template<> inline bool Stream::Serialize(bool& var)
{
    if (IsWriting())
    {
        unsigned char temp = var ? 1 : 0;
        if (!Serialize(temp))
        {
            Truncate(); // In case override forgets to do this.
            return false;
        }
    }
    else
    {
        unsigned char temp = 0;
        if (!Serialize(temp))
        {
            Truncate(); // In case override forgets to do this.
            return false;
        }
        var = temp != 0;
    }

    return true;
}

template<> inline bool Stream::Serialize(const bool& var)
{
    if (!IsWriting())
    {
        Truncate();
        return false;
    }

    unsigned char temp = var ? 1 : 0;
    if (!Serialize(temp))
    {
        Truncate(); // In case override forgets to do this.
        return false;
    }

    return true;
}

template<> inline bool Stream::Serialize(std::string& var)
{
    int len = (int)var.size();
    if (len < 0)
        return false;

    if (!Serialize(len))
        return false;

    u8* block = GetBlock(len);
    if (!block)
        return false;

    if (IsWriting())
    {
#ifdef _MSC_VER
        var._Copy_s((char*)block, len, len);
#else
        var.copy((char*)block, len);
#endif
    }
    else
        var.assign((char*)block, len);

    return true;
}

template<> inline bool Stream::Serialize(const std::string& var)
{
    if (!IsWriting())
    {
        Truncate();
        return false;
    }

    int len = (int)var.size();
    if (len < 0)
        return false;

    if (!Serialize(len))
        return false;

    u8* block = GetBlock(len);
    if (!block)
        return false;

#ifdef _MSC_VER
    var._Copy_s((char*)block, len, len);
#else
    var.copy((char*)block, len);
#endif

    return true;
}

template<> inline bool Stream::Serialize(const char*& var)
{
    if (!IsWriting())
    {
        Truncate();
        return false;
    }

    int len = (int)strlen(var);
    if (len < 0)
        return false;

    if (!Serialize(len))
        return false;

    u8* block = GetBlock(len);
    if (!block)
        return false;

    memcpy(block, var, len);

    return true;
}

template<typename T>
inline bool Stream::SerializeVec(std::vector<T>& vec)
{
    if (IsWriting())
    {
        return SerializeVec((const std::vector<T>&)vec);
    }
    else // Reading:
    {
        int32_t count = 0;
        if (!Serialize(count))
        {
            return false;
        }

        int32_t len = 0;
        if (!Serialize(len))
        {
            return false;
        }

        if ((size_t)len != count * sizeof(T))
        {
            // Size mismatch.
            return false;
        }

        char* block = GetBlock(len);
        if (!block)
        {
            return false;
        }

        vec.resize(count);
        memcpy(vec.data(), block, len);

        return true;
    }
}

template<typename T>
inline bool Stream::SerializeVec(const std::vector<T>& vec)
{
    if (!IsWriting())
    {
        Truncate();
        return false;
    }

    int32_t count = (int32_t)vec.size();
    // Limit to 2^32 elements.
    if (count < 0 || (size_t)count != vec.size())
    {
        return false;
    }

    int32_t len = count * sizeof(T);
    // Basic length sanity check too, limit to 2^32 bytes.
    if ((size_t)len != count * sizeof(T))
    {
        return false;
    }

    if (!Serialize(count))
    {
        return false;
    }

    if (!Serialize(len))
    {
        return false;
    }

    char* block = GetBlock(len);
    if (!block)
    {
        return false;
    }

    memcpy(block, vec.data(), len);

    return true;
}

template<typename T>
inline bool Stream::SerializeVecView(vector_view<T>& vec)
{
    if (IsWriting())
    {
        return SerializeVecView((const vector_view<T>&)vec);
    }
    else // Reading:
    {
        int32_t count = 0;
        if (!Serialize(count))
        {
            return false;
        }

        int32_t len = 0;
        if (!Serialize(len))
        {
            return false;
        }

        if ((size_t)len != count * sizeof(T))
        {
            // Type size mismatch.
            return false;
        }

        char* block = GetBlock(len);
        if (!block)
        {
            return false;
        }

        vec.buffer = reinterpret_cast<T*>(block);
        vec.count = count;

        return true;
    }
}

template<typename T>
inline bool Stream::SerializeVecView(const vector_view<T>& vec)
{
    if (!IsWriting())
    {
        Truncate();
        return false;
    }

    int32_t count = (int32_t)vec.size();
    // Limit to 2^32 elements.
    if (count < 0 || (size_t)count != vec.size())
    {
        return false;
    }

    int32_t len = count * sizeof(T);
    // Basic length sanity check too, limit to 2^32 bytes.
    if ((size_t)len != count * sizeof(T))
    {
        return false;
    }

    if (!Serialize(count))
    {
        return false;
    }

    if (!Serialize(len))
    {
        return false;
    }

    char* block = GetBlock(len);
    if (!block)
    {
        return false;
    }

    memcpy(block, vec.data(), len);

    return true;
}


//-----------------------------------------------------------------------------
// Serialize() free function
//
// This is the recommended way to serialize data outside of this module.
// The advantage is that it will use good default serializers specified here
// for integral types (int, bool, char*) and for user-defined structures with
// a Serialize() method it will invoke that instead.

template <typename T>
class has_serialize // C++11 trait
{
private:
    template <typename U, U>
    class check
    { };
    
    template <typename C>
    static char f(check<bool (C::*)(Stream&), &C::Serialize>*);

    template <typename C>
    static long f(...);
    
public:
    static const bool value = (sizeof(f<T>(0)) == sizeof(char));
};

template <typename T>
inline bool Serialize(Stream& stream, T& val,
                      typename std::enable_if<!has_serialize<T>::value && !std::is_enum<T>::value, T>::type* = nullptr)
{
    // General purpose version for types without Serialize() members and non-enumeration types.
    return stream.Serialize(val);
}

template <typename T>
inline bool Serialize(Stream& stream, const T&& val,
                      typename std::enable_if<!has_serialize<T>::value && !std::is_enum<T>::value, T>::type* = nullptr)
{
    // General purpose version for types without Serialize() members and non-enumeration types.
    return stream.Serialize(val);
}

template <typename T>
inline bool Serialize(Stream& stream, T& val,
                      typename std::enable_if<!has_serialize<T>::value && std::is_enum<T>::value, T>::type* = nullptr)
{
    // Special version for enumeration types.
    return stream.SerializeAs<T, uint64_t>(val);
}

template <typename T>
inline bool Serialize(Stream& stream, const T&& val,
                      typename std::enable_if<!has_serialize<T>::value && std::is_enum<T>::value, T>::type* = nullptr)
{
    // Special version for enumeration types.
    return stream.SerializeAs<T, uint64_t>(val);
}

template <typename T>
inline bool Serialize(Stream& stream, T& val,
                      typename std::enable_if<has_serialize<T>::value, T>::type* = nullptr)
{
    // Specialized version when the type has a Serialize() member.
    if (!val.Serialize(stream))
    {
        stream.Truncate(); // In case override forgets to do this.
        return false;
    }

    return true;
}

template <typename T>
inline bool Serialize(Stream& stream, const T&& val,
                      typename std::enable_if<has_serialize<T>::value, T>::type* = nullptr)
{
    // Make sure we aren't expecting to deserialize to a const variable.
    if (!stream.IsWriting())
    {
        stream.Truncate();
        return false;
    }

    // Specialized version when the type has a Serialize() member.
    T* pval = const_cast<T*>( &val );
    if (!pval->Serialize(stream))
    {
        stream.Truncate(); // In case override forgets to do this.
        return false;
    }

    return true;
}
