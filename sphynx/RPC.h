#pragma once

#include "Tools.h"
#include "Stream.h"
#include <unordered_map>
#include <memory>
#include <functional>
#include <type_traits>
#include <utility>
#include <algorithm> // std::remove


//-----------------------------------------------------------------------------
// base_param_trait<T>

template <typename T>
struct base_param_trait
{
    typedef typename std::remove_reference<T>::type T_no_ref;
    typedef typename std::remove_const<T_no_ref>::type type;
};


//-----------------------------------------------------------------------------
// InputSerializer
//
// Serializes the input arguments from a parameter pack from the provided
// Stream object.
//
// if (!InputSerializer::SerializeInputs<Args...>(stream, args...))
//    return false;

struct InputSerializer
{
    template < typename T >
    static FORCE_INLINE bool SerializeInputs(Stream & stream, T first)
    {
        return InputSerializerImpl<T>::SerializeInput(stream, first);
    }

    template < typename T, typename... Args >
    static FORCE_INLINE bool SerializeInputs(Stream & stream, T first, Args... args)
    {
        if (!InputSerializerImpl<T>::SerializeInput(stream, first))
            return false;

        return SerializeInputs<Args...>(stream, args...);
    }

    template < typename T = void, typename... Args >
    static FORCE_INLINE bool SerializeInputs(Stream &)
    {
        return true;
    }

protected:
    template < typename T >
    struct InputSerializerImpl
    {
        static FORCE_INLINE bool SerializeInput(Stream & stream, T& first)
        {
            return Serialize(stream, first);
        }
    };
};


//-----------------------------------------------------------------------------
// function_traits<F>
//
// Allow analysis of a provided function type.

template <typename T>
struct function_traits
    : public function_traits<decltype(&T::operator())>
{};

template <typename ReturnType, typename... Args>
struct function_traits<ReturnType(Args...)>
{
    // function_traits<F>::return_type is the type of the return value.
    typedef ReturnType return_type;

    // function_traits<F>::function_type is the type of the function without other qualifiers.
    typedef ReturnType function_type(Args...);

    // function_traits<F>::arg_count is the number of arguments.
    enum { arg_count = sizeof...(Args) };

    // function_traits<F>::arg<N>::type is the type of the Nth argument.
    template <size_t i>
    struct arg
    {
        template <typename F, bool>
        struct available
        {
            typedef void type;
        };
        template <typename F>
        struct available < F, true >
        {
            typedef typename std::tuple_element<i, std::tuple<Args...>>::type type;
        };

        typedef typename available<ReturnType, (i < sizeof...(Args))>::type type;
    };
};

template <typename ReturnType, typename... Args>
struct function_traits<ReturnType(*)(Args...)>
    : public function_traits<ReturnType(Args...)>
{};

template <typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType(ClassType::*)(Args...)>
    : public function_traits<ReturnType(Args...)>
{
    typedef ClassType& owner_type;
};

template <typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType(ClassType::*)(Args...) const>
    : public function_traits<ReturnType(Args...)>
{
    typedef const ClassType& owner_type;
};

template <typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType(ClassType::*)(Args...) volatile>
    : public function_traits<ReturnType(Args...)>
{
    typedef volatile ClassType& owner_type;
};

template <typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType(ClassType::*)(Args...) const volatile>
    : public function_traits<ReturnType(Args...)>
{
    typedef const volatile ClassType& owner_type;
};

template <typename FunctionType>
struct function_traits<std::function<FunctionType>>
    : public function_traits<FunctionType>
{};

template <typename T>
struct function_traits<T&> : public function_traits<T>{};
template <typename T>
struct function_traits<const T&> : public function_traits<T>{};
template <typename T>
struct function_traits<volatile T&> : public function_traits<T>{};
template <typename T>
struct function_traits<const volatile T&> : public function_traits<T>{};
template <typename T>
struct function_traits<T&&> : public function_traits<T>{};
template <typename T>
struct function_traits<const T&&> : public function_traits<T>{};
template <typename T>
struct function_traits<volatile T&&> : public function_traits<T>{};
template <typename T>
struct function_traits<const volatile T&&> : public function_traits<T>{};


//-----------------------------------------------------------------------------
// CallSerializer

template < int kCallId, typename T >
struct CallSerializer
{
};

template < int kCallId, typename... Args >
struct CallSerializer < kCallId, void(Args...) >
{
    std::function<void(Stream&)> CallSender;

    bool operator()(Args... args)
    {
        // Wrap a local buffer to construct the packet
        u8 writeBuffer[512];
        Stream stream;
        stream.WrapWrite(writeBuffer, sizeof(writeBuffer));

        // Write call id byte
        u8 callId = static_cast<u8>(kCallId);
        stream.Serialize(callId);

        // Serialize function parameters
        if (!InputSerializer::SerializeInputs(stream, args...))
        {
            DEBUG_BREAK;
            return false;
        }

        // Parameters should never exceed 255 bytes
        if (stream.IsDynamic())
        {
            DEBUG_BREAK;
            return false;
        }

        CallSender(stream);
        return true;
    }
};


//-----------------------------------------------------------------------------
// add_output_ptr< T, is_input_arg<T> >::Convert()
//
// This function will add a reference to function call parameters that are of
// output type.  This is useful during deserialization of calls.

template<typename T>
struct add_output_ptr_impl
{
    typedef typename std::remove_reference<T>::type T_no_ref;
    typedef typename std::remove_const< T_no_ref >::type T_no_const;
    typedef typename std::add_lvalue_reference< T_no_const >::type nctype;

    static nctype& Convert(nctype& param)
    {
        return param;
    }
};

template<typename ArgType>
struct add_output_ptr_t
{
    typedef typename base_param_trait<ArgType>::type base_param_t;
    typedef add_output_ptr_impl<base_param_t> type;
};


//-----------------------------------------------------------------------------
// add_input_ref< T, is_input_arg<T> >::type
//
// This trait will add a reference to the type if it is an input type.

template<typename T>
struct add_input_ref
{
    typedef typename std::remove_reference<T>::type T_no_ref;
    typedef typename std::remove_const< T_no_ref >::type T_no_const;
    typedef typename std::add_lvalue_reference< T_no_const >::type type;
};

template<typename ArgType>
struct add_input_ref_t
{
    typedef typename add_input_ref< ArgType>::type type;
};


//-----------------------------------------------------------------------------
// CallDeserializer
//
// Wraps a function definition.  Must be added to a connection via the Register
// function on the Connection object.
//
// Note that call arguments are either inputs or output but never both: by-ref
// arguments are interpreted as inputs, and non-const pointers are outputs

class BaseCallDeserializer
{
public:
    virtual bool WrappedCall(Stream& input) = 0;
};


template<typename F, int ArgCount>
class CallDeserializer
{
};

template<typename F>
class CallDeserializer<F, 0> : public BaseCallDeserializer
{
public:
    typedef typename function_traits<F>::function_type ftype;
    typedef std::function<ftype> HandlerT;
    HandlerT Handler;

    bool WrappedCall(Stream& input) override
    {
        Handler();
        return true;
    }
};

template<typename F>
class CallDeserializer<F, 1> : public BaseCallDeserializer
{
public:
    typedef typename function_traits<F>::function_type ftype;
    typedef std::function<ftype> HandlerT;
    HandlerT Handler;

    bool WrappedCall(Stream& input) override
    {
        typedef typename function_traits<F>::template arg<0>::type A0;

        typename base_param_trait<A0>::type a0;

        if (!InputSerializer::SerializeInputs<
            typename add_input_ref_t<A0>::type>(
            input,
            add_output_ptr_t<A0>::type::Convert(a0)))
        {
            DEBUG_BREAK; return false;
        }

        Handler(
            add_output_ptr_t<A0>::type::Convert(a0));
        return true;
    }
};

template<typename F>
class CallDeserializer<F, 2> : public BaseCallDeserializer
{
public:
    typedef typename function_traits<F>::function_type ftype;
    typedef std::function<ftype> HandlerT;
    HandlerT Handler;

    bool WrappedCall(Stream& input) override
    {
        typedef typename function_traits<F>::template arg<0>::type A0;
        typedef typename function_traits<F>::template arg<1>::type A1;

        typename base_param_trait<A0>::type a0;
        typename base_param_trait<A1>::type a1;

        if (!InputSerializer::SerializeInputs<
            typename add_input_ref_t<A0>::type,
            typename add_input_ref_t<A1>::type>(
            input,
            add_output_ptr_t<A0>::type::Convert(a0),
            add_output_ptr_t<A1>::type::Convert(a1)))
        {
            DEBUG_BREAK; return false;
        }

        Handler(
            add_output_ptr_t<A0>::type::Convert(a0),
            add_output_ptr_t<A1>::type::Convert(a1));
        return true;
    }
};

template<typename F>
class CallDeserializer<F, 3> : public BaseCallDeserializer
{
public:
    typedef typename function_traits<F>::function_type ftype;
    typedef std::function<ftype> HandlerT;
    HandlerT Handler;

    bool WrappedCall(Stream& input) override
    {
        typedef typename function_traits<F>::template arg<0>::type A0;
        typedef typename function_traits<F>::template arg<1>::type A1;
        typedef typename function_traits<F>::template arg<2>::type A2;

        typename base_param_trait<A0>::type a0;
        typename base_param_trait<A1>::type a1;
        typename base_param_trait<A2>::type a2;

        if (!InputSerializer::SerializeInputs<
            typename add_input_ref_t<A0>::type,
            typename add_input_ref_t<A1>::type,
            typename add_input_ref_t<A2>::type>(
            input,
            add_output_ptr_t<A0>::type::Convert(a0),
            add_output_ptr_t<A1>::type::Convert(a1),
            add_output_ptr_t<A2>::type::Convert(a2)))
        {
            DEBUG_BREAK; return false;
        }

        Handler(
            add_output_ptr_t<A0>::type::Convert(a0),
            add_output_ptr_t<A1>::type::Convert(a1),
            add_output_ptr_t<A2>::type::Convert(a2));
        return true;
    }
};

template<typename F>
class CallDeserializer<F, 4> : public BaseCallDeserializer
{
public:
    typedef typename function_traits<F>::function_type ftype;
    typedef std::function<ftype> HandlerT;
    HandlerT Handler;

    bool WrappedCall(Stream& input) override
    {
        typedef typename function_traits<F>::template arg<0>::type A0;
        typedef typename function_traits<F>::template arg<1>::type A1;
        typedef typename function_traits<F>::template arg<2>::type A2;
        typedef typename function_traits<F>::template arg<3>::type A3;

        typename base_param_trait<A0>::type a0;
        typename base_param_trait<A1>::type a1;
        typename base_param_trait<A2>::type a2;
        typename base_param_trait<A3>::type a3;

        if (!InputSerializer::SerializeInputs<
            typename add_input_ref_t<A0>::type,
            typename add_input_ref_t<A1>::type,
            typename add_input_ref_t<A2>::type,
            typename add_input_ref_t<A3>::type>(
            input,
            add_output_ptr_t<A0>::type::Convert(a0),
            add_output_ptr_t<A1>::type::Convert(a1),
            add_output_ptr_t<A2>::type::Convert(a2),
            add_output_ptr_t<A3>::type::Convert(a3)))
        {
            DEBUG_BREAK; return false;
        }

        Handler(
            add_output_ptr_t<A0>::type::Convert(a0),
            add_output_ptr_t<A1>::type::Convert(a1),
            add_output_ptr_t<A2>::type::Convert(a2),
            add_output_ptr_t<A3>::type::Convert(a3));
        return true;
    }
};

template<typename F>
class CallDeserializer<F, 5> : public BaseCallDeserializer
{
public:
    typedef typename function_traits<F>::function_type ftype;
    typedef std::function<ftype> HandlerT;
    HandlerT Handler;

    bool WrappedCall(Stream& input) override
    {
        typedef typename function_traits<F>::template arg<0>::type A0;
        typedef typename function_traits<F>::template arg<1>::type A1;
        typedef typename function_traits<F>::template arg<2>::type A2;
        typedef typename function_traits<F>::template arg<3>::type A3;
        typedef typename function_traits<F>::template arg<4>::type A4;

        typename base_param_trait<A0>::type a0;
        typename base_param_trait<A1>::type a1;
        typename base_param_trait<A2>::type a2;
        typename base_param_trait<A3>::type a3;
        typename base_param_trait<A4>::type a4;

        if (!InputSerializer::SerializeInputs<
            typename add_input_ref_t<A0>::type,
            typename add_input_ref_t<A1>::type,
            typename add_input_ref_t<A2>::type,
            typename add_input_ref_t<A3>::type,
            typename add_input_ref_t<A4>::type>(
            input,
            add_output_ptr_t<A0>::type::Convert(a0),
            add_output_ptr_t<A1>::type::Convert(a1),
            add_output_ptr_t<A2>::type::Convert(a2),
            add_output_ptr_t<A3>::type::Convert(a3),
            add_output_ptr_t<A4>::type::Convert(a4)))
        {
            DEBUG_BREAK; return false;
        }

        Handler(
            add_output_ptr_t<A0>::type::Convert(a0),
            add_output_ptr_t<A1>::type::Convert(a1),
            add_output_ptr_t<A2>::type::Convert(a2),
            add_output_ptr_t<A3>::type::Convert(a3),
            add_output_ptr_t<A4>::type::Convert(a4));
        return true;
    }
};


//-----------------------------------------------------------------------------
// MakeCallDeserializer
//
// Auto-detect and create the type of call deserializer for a function.

template<typename F>
std::shared_ptr<BaseCallDeserializer> MakeCallDeserializer(const std::function<F> handler)
{
    typedef CallDeserializer<F, function_traits<F>::arg_count> CType;

    auto specific = std::make_shared<CType>();
    specific->Handler = handler;

    std::shared_ptr<BaseCallDeserializer> ptr = specific;
    return ptr;
}


//-----------------------------------------------------------------------------
// CallRouter

class CallRouter
{
public:
    template<typename F>
    void Set(u8 callId, const std::function<F> handler)
    {
        Locker locker(CallLock);
        CallTable[callId] = MakeCallDeserializer(handler);
    }
    void Clear(u8 callId)
    {
        Locker locker(CallLock);
        CallTable[callId].reset();
    }
    void Clear()
    {
        Locker locker(CallLock);
        for (auto& call : CallTable)
            call.reset();
    }
    bool Call(Stream& input)
    {
        u8 callId;
        if (!input.Serialize(callId))
            return false;

        Locker locker(CallLock);

        auto& call = CallTable[callId];
        if (!call)
            return false;

        return call->WrappedCall(input);
    }

protected:
    Lock CallLock;
    std::shared_ptr<BaseCallDeserializer> CallTable[256];
};
