// A simple count down timer

#ifndef _ASYNC_TIMER
#define _ASYNC_TIMER

class AsyncTimer
{
  public:
    AsyncTimer(const unsigned long duration = 0)
    {
      m_start = 0;
      m_duration = duration;
      m_bIsActive = false;
    }

    inline void Start() { Start(m_duration); }

    inline void Stop() { m_bIsActive = false; }

    inline bool IsActive() { return m_bIsActive; }

    inline unsigned long GetTimeLeft()
    {
       return _max(0, int32_t(m_duration) - int32_t(millis() - m_start));
    }

    void Start(const unsigned long duration)
    {
      m_bIsActive = true;
      m_duration = duration;
      m_start = millis();
    }

    bool RanOut()
    {
      if ((millis() - m_start) > m_duration)
      {
        return true;
      }

      return false;
    }

  private:
    unsigned long m_duration;
    unsigned long m_start;
    bool m_bIsActive;
};

#endif  // _ASYNC_TIMER