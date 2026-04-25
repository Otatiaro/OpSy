#include "CriticalSection.hpp"
#include "Scheduler.hpp"


opsy::CriticalSection::~CriticalSection()
{
	if (m_valid)
		Scheduler::criticalSectionEnd();
}
