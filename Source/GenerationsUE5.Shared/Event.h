#pragma once

#include <Windows.h>

struct Event
{
protected:
    HANDLE m_handle = nullptr;

public:
    static constexpr TCHAR s_cpuEventName[] = TEXT("GenerationsUE5CPUEvent");
    static constexpr TCHAR s_gpuEventName[] = TEXT("GenerationsUE5GPUEvent");

    Event(LPCTSTR name, BOOL initialState);
    Event(LPCTSTR name);
    ~Event();

    void wait() const;
    bool waitImm() const;

    void set() const;
    void reset() const;
};

#include "Event.inl"