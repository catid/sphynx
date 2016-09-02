#pragma once

#include <vector>


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
    int x = 0, y = 0;

    // Sorted linked list
    Object* x_prev = nullptr;
    Object* x_next = nullptr;
    bool Enlisted = false;
};


//-----------------------------------------------------------------------------
// NeighborTracker

template<class Object> class NeighborTracker
{
public:
    void Remove(Object* node);
    void Update(Object* node, int x, int y);

    // Note: Returns a held Locker that should be released when done reading the neighbors
    void GetNeighbors(Object* node, int distance, std::vector<Object*>& neighbors, ReadLocker& locker) const;

protected:
    mutable RWLock ListLock;
    Object* Head = nullptr;

    // Returns true if we won the race to insert, or false if we lost and should update instead
    // Note: Must be called with lock *not* held
    bool racyInsertion(Object* node, int x, int y);
};


//-----------------------------------------------------------------------------
// NeighborTracker

template<class Object>
void NeighborTracker<Object>::Remove(Object* node)
{
    WriteLocker heavyLocker(ListLock);

    // If not already removed:
    if (node->Neighbor.Enlisted)
    {
        node->Neighbor.Enlisted = false;

        Object* next = node->Neighbor.x_next;
        Object* prev = node->Neighbor.x_prev;

        // Unlink
        if (next)
            next->Neighbor.x_prev = prev;
        if (prev)
            prev->Neighbor.x_next = next;
        else
            Head = next;
    }
}

template<class Object>
void NeighborTracker<Object>::Update(Object* node, int x, int y)
{
    ReadLocker locker(ListLock);

    // We could spin here for a while since we cannot promote from read to write directly
    while (!node->Neighbor.Enlisted)
    {
        // Release lock and try to insert (we may lose this race):
        locker.Clear();
        if (racyInsertion(node, x, y))
            return;
        // If we're updating after all:
        locker.Set(ListLock);
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
                    next->Neighbor.x_next = node;
                    node->Neighbor.x_next = nullptr;
                    node->Neighbor.x_prev = next;
                    break;
                }

                // If nextnext should be to the right:
                if (nextnext->Neighbor.x >= x)
                {
                    node->Neighbor.x_next = nextnext;
                    node->Neighbor.x_prev = next;
                    next->Neighbor.x_next = node;
                    nextnext->Neighbor.x_prev = node;
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
                    prev->Neighbor.x_prev = node;
                    node->Neighbor.x_next = prev;
                    node->Neighbor.x_prev = nullptr;
                    Head = node;
                    break;
                }

                // If prevprev should be to the left:
                if (prevprev->Neighbor.x <= x)
                {
                    node->Neighbor.x_next = prev;
                    node->Neighbor.x_prev = prevprev;
                    prev->Neighbor.x_prev = node;
                    prevprev->Neighbor.x_next = node;
                    break;
                }

                prev = prevprev;
            }
        }
    }
}

template<class Object>
void NeighborTracker<Object>::GetNeighbors(Object* node, int distance, std::vector<Object*>& neighbors, ReadLocker& locker) const
{
    neighbors.clear();

    locker.Set(ListLock);

    if (!node->Neighbor.Enlisted)
    {
        locker.Clear();
        return; // Not in the list
    }

    const int x = node->Neighbor.x, y = node->Neighbor.y;

    for (Object* prev = node->Neighbor.x_prev; prev; prev = prev->Neighbor.x_prev)
    {
        if (x - prev->Neighbor.x > distance)
            break;
        if (std::abs(y - prev->Neighbor.y) <= distance)
            neighbors.push_back(prev);
    }
    for (Object* next = node->Neighbor.x_next; next; next = next->Neighbor.x_next)
    {
        if (next->Neighbor.x - x > distance)
            break;
        if (std::abs(y - next->Neighbor.y) <= distance)
            neighbors.push_back(next);
    }
}

template<class Object>
bool NeighborTracker<Object>::racyInsertion(Object* node, int x, int y)
{
    // Must NOT be called with ListLock held
    WriteLocker heavyLocker(ListLock);

    // If we lost the race to insert:
    if (node->Neighbor.Enlisted)
        return false;

    node->Neighbor.Enlisted = true;
    node->Neighbor.x = x;
    node->Neighbor.y = y;

    // Insert at head if list is empty:
    Object* next = Head;
    if (!next)
    {
        Head = node;
        node->Neighbor.x_next = nullptr;
        node->Neighbor.x_prev = nullptr;
        return true;
    }

    // If we should insert at head:
    if (next->Neighbor.x >= x)
    {
        node->Neighbor.x_next = next;
        node->Neighbor.x_prev = nullptr;
        next->Neighbor.x_prev = node;
        Head = node;
        return true;
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
            next->Neighbor.x_next = node;
            break;
        }

        // If we should insert between 'next' and 'nextnext':
        if (node->Neighbor.x >= x)
        {
            node->Neighbor.x_next = nextnext;
            node->Neighbor.x_prev = next;
            next->Neighbor.x_next = node;
            nextnext->Neighbor.x_prev = node;
            break;
        }

        next = nextnext;
    }

    return true;
}
