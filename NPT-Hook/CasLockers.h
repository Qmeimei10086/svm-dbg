#ifndef CAS_LOCKERS_H
#define CAS_LOCKERS_H

#include "Basic.h"

//线程不可冲入互斥量，基于原子操作
class SignleLockMutex
{
    LONG _locker;

public:
    SignleLockMutex() : _locker(0) {}
    //阻塞抢锁
    void Lock()
    {
        while (InterlockedCompareExchange(&_locker, TRUE, FALSE))
            continue;
    }
    //尝试抢锁
    bool TryLock()
    {
        return !InterlockedCompareExchange(&_locker, TRUE, FALSE);
    }
    //解锁
    void Unlock()
    {
        InterlockedCompareExchange(&_locker, FALSE, TRUE);
    }
};

//读写锁，线程不可重入
class ReadWriteLock
{
    const LONG ReadModeId = 2;
    const LONG WriteModeId = 1;
    const LONG UnlockModeId = 0;

    LONG _count;
    LONG _id;
public:
    ReadWriteLock() : _count(0), _id(0) {}
    // 阻塞加读锁
    void ReadLock()
    {
        LONG previousId = UnlockModeId;
        do
        {
            previousId = InterlockedCompareExchange(&_id, ReadModeId, UnlockModeId);
        } while (previousId != UnlockModeId && previousId != ReadModeId);
        InterlockedIncrement(&_count);
    }
    // 尝试加读锁
    bool TryRead()
    {
        LONG previousId = UnlockModeId;
        previousId = InterlockedCompareExchange(&_id, ReadModeId, UnlockModeId);
        if (previousId == UnlockModeId || previousId == ReadModeId)
        {
            InterlockedIncrement(&_count);
            return true;
        }
        else
        {
            return false;
        }
    }
    // 解除读锁
    void ReadUnlock()
    {
        if (InterlockedCompareExchange(&_id, ReadModeId, ReadModeId) == ReadModeId)
        {
            if (InterlockedDecrement(&_count) == 0)
                InterlockedCompareExchange(&_id, UnlockModeId, ReadModeId);
        }
    }
    // 阻塞加写锁
    void WriteLock()
    {
        LONG previousId = 0;
        do
        {
            previousId = InterlockedCompareExchange(&_id, WriteModeId, UnlockModeId);
        } while (previousId != UnlockModeId);
        InterlockedExchange(&_count, 0);
    }
    // 尝试加写锁
    bool TryWrite()
    {
        bool result = InterlockedCompareExchange(&_id, WriteModeId, UnlockModeId) == UnlockModeId;
        if (result)
            InterlockedExchange(&_count, 0);
        return result;
    }
    // 解除写锁
    void WriteUnlock()
    {
        InterlockedCompareExchange(&_id, UnlockModeId, WriteModeId);
    }
};

#endif