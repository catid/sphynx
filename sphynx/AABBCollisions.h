#include <iostream>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
using namespace std;


//-----------------------------------------------------------------------------
// NeighborInfo
//
// This should be attached to each object
template<class Object> class NeighborTracker;

template<class Object> class NeighborInfo
{
protected:
    friend class NeighborTracker<Object>; 

    // Update these via NeighborTracker::Update()

    // 2D object position
    int x, y;

    // Sorted linked list
    Object* x_prev;
    Object* x_next;

    // Self-reference to avoid reference keeping elsewhere in the algorithm
    std::shared_ptr<Object> self_ref;
};


//-----------------------------------------------------------------------------
// NeighborTracker

template<class Object> class NeighborTracker
{
public:
    void Remove(const std::shared_ptr<Object>& node);
    void Update(const std::shared_ptr<Object>& node, int x, int y);
    void GetNeighbors(const std::shared_ptr<Object>& node, int distance, std::vector<std::shared_ptr<Object>>& neighbors) const;

protected:
    mutable std::mutex Lock;
    Object* Head = nullptr;

    void insert(const std::shared_ptr<Object>& node, int x, int y);
};


//-----------------------------------------------------------------------------
// NeighborTracker

template<class Object>
void NeighborTracker<Object>::Remove(const std::shared_ptr<Object>& node)
{
    std::shared_ptr<Object> finalRef;
    {
        std::lock_guard<std::mutex> locker(Lock);

        finalRef = node->Neighbor.self_ref;

        // If not already removed:
        if (finalRef)
        {
            Object* next = node->Neighbor.x_next;
            Object* prev = node->Neighbor.x_prev;

            // Unlink
            if (next)
                next->Neighbor.x_prev = prev;
            if (prev)
                prev->Neighbor.x_next = next;
            else
                Head = next;

            node->Neighbor.self_ref = nullptr;
        }
    }
    finalRef = nullptr; // Release final reference outside of lock
}

template<class Object>
void NeighborTracker<Object>::Update(const std::shared_ptr<Object>& node, int x, int y)
{
    std::lock_guard<std::mutex> locker(Lock);

    if (!node->Neighbor.self_ref)
    {
        insert(node, x, y);
        return;
    }

    // Update (common case):

    const int old_x = node->Neighbor.x;
    node->Neighbor.x = x;
    node->Neighbor.y = y;

    // If the object moved to the right, then it may need to move right in the list:
    if (x > old_x)
    {
        // If the immediate right neighbor is now to the left:
        Object* next = node->Neighbor.x_next;
        if (next && next->Neighbor.x < x)
        {
            // Unlink
            Object* prev = node->Neighbor.x_prev;
            next->Neighbor.x_prev = prev;
            if (prev)
                prev->Neighbor.x_next = next;
            else
                Head = next;

            for (;;)
            {
                Object* nextnext = next->Neighbor.x_next;

                // If nothing is after next, insert at end:
                if (!nextnext)
                {
                    next->Neighbor.x_next = node.get();
                    node->Neighbor.x_next = nullptr;
                    node->Neighbor.x_prev = next;
                    break;
                }

                // If nextnext should be to the right:
                if (nextnext->Neighbor.x >= x)
                {
                    node->Neighbor.x_next = nextnext;
                    node->Neighbor.x_prev = next;
                    next->Neighbor.x_next = node.get();
                    nextnext->Neighbor.x_prev = node.get();
                    break;
                }

                next = nextnext;
            }
        }
    }
    else // It (probably) moved to the left. It may need to move left in the list:
    {
        // If the immediate left neighbor is now to the right:
        Object* prev = node->Neighbor.x_prev;
        if (prev && prev->Neighbor.x > x)
        {
            // Unlink
            Object* next = node->Neighbor.x_next;
            if (next)
                next->Neighbor.x_prev = prev;
            prev->Neighbor.x_next = next;

            for (;;)
            {
                Object* prevprev = prev->Neighbor.x_prev;

                // If nothing is before prev, insert at head:
                if (!prevprev)
                {
                    prev->Neighbor.x_prev = node.get();
                    node->Neighbor.x_next = prev;
                    node->Neighbor.x_prev = nullptr;
                    Head = node.get();
                    break;
                }

                // If prevprev should be to the left:
                if (prevprev->Neighbor.x <= x)
                {
                    node->Neighbor.x_next = prev;
                    node->Neighbor.x_prev = prevprev;
                    prev->Neighbor.x_prev = node.get();
                    prevprev->Neighbor.x_next = node.get();
                    break;
                }

                prev = prevprev;
            }
        }
    }
}

template<class Object>
void NeighborTracker<Object>::GetNeighbors(const std::shared_ptr<Object>& node, int distance, std::vector<std::shared_ptr<Object>>& neighbors) const
{
    neighbors.clear();

    std::lock_guard<std::mutex> locker(Lock);

    if (!node->Neighbor.self_ref)
        return; // Not in the list

    const int x = node->Neighbor.x, y = node->Neighbor.y;

    for (Object* prev = node->Neighbor.x_prev; prev; prev = prev->Neighbor.x_prev)
    {
        if (x - prev->Neighbor.x > distance)
            break;
        if (std::abs(y - prev->Neighbor.y) <= distance)
            neighbors.push_back(prev->Neighbor.self_ref);
    }
    for (Object* next = node->Neighbor.x_next; next; next = next->Neighbor.x_next)
    {
        if (next->Neighbor.x - x > distance)
            break;
        if (std::abs(y - next->Neighbor.y) <= distance)
            neighbors.push_back(next->Neighbor.self_ref);
    }
}

template<class Object>
void NeighborTracker<Object>::insert(const std::shared_ptr<Object>& node, int x, int y)
{
    // Must be called with Lock held

    node->Neighbor.self_ref = node;
    node->Neighbor.x = x;
    node->Neighbor.y = y;

    // Insert at head if list is empty:
    Object* next = Head;
    if (!next)
    {
        Head = node.get();
        node->Neighbor.x_next = nullptr;
        node->Neighbor.x_prev = nullptr;
        return;
    }

    // If we should insert at head:
    if (next->Neighbor.x >= x)
    {
        node->Neighbor.x_next = next;
        node->Neighbor.x_prev = nullptr;
        next->Neighbor.x_prev = node.get();
        Head = node.get();
        return;
    }

    // Walk list from left-to-right searching for insertion point:
    for (;;)
    {
        Object* nextnext = next->Neighbor.x_next;

        // If we ran out of nodes, insert at end:
        if (!nextnext)
        {
            node->Neighbor.x_next = nullptr;
            node->Neighbor.x_prev = next;
            next->Neighbor.x_next = node.get();
            break;
        }

        // If we should insert between 'next' and 'nextnext':
        if (node->Neighbor.x >= x)
        {
            node->Neighbor.x_next = nextnext;
            node->Neighbor.x_prev = next;
            next->Neighbor.x_next = node.get();
            nextnext->Neighbor.x_prev = node.get();
            break;
        }

        next = nextnext;
    }
}
