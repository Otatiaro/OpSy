<p align="center"> 
<img src="https://eznovsas.github.io/OpSy/LOGOOPSY256.png">
</p>

# OpSy

OpSy is a Real Time Operating System or RTOS for Cortex-M microcontrollers.
It is designed to support Cortex-M3, Cortex-M4, Cortex-M7 and probably Cortex-M33 in the future.

It is entirely written in C++17, and with GCC in mind.
The typical target compiler is ARM GCC, and it has been developed using version 8-2018-q4-major.

The main goal is to make it as easy as possible to use with STM32 targets.

Code documentation is available at https://eznovsas.github.io/OpSy/

# Basic how to

For now, if you want to use OpSy with Atollic, you need to update the toolchain to ARM GCC 8-2018-q4-major.
I will not detail this procedure because a far better alternative is coming.

Once you have a project with C++17 enabled, using OpSy is 4 very simple steps:
- add files in your project, the git is designed to be included as a git submodule
- add `#include <opsy.hpp>` at the beginning of your `main.cpp` files
- at the end of your main function, simply add `opsy::Scheduler::start();`

That's it, if your run it, the scheduler will take control at the end of your main function, and most probably go to idle because there is no task to run. Done.

Ok maybe we can do a bit more, like a blinky (blink an LED), for that you will need the code to configure the output pin, and code to set and reset the pin.
Then follow these steps:
- add the pin configuration code in your main before the call to `Scheduler::start`
- right after your `#include` statements, add `using namespace std::chrono_literals;` this allows to use the C++ time related literals to easily express seconds, milliseconds, etc.
- before main, declares a global variable like this : `opsy::Task<512> task;`. The 512 is the size reserved for your task stack, 512 is plenty, a lot more than we need to blink an LED.
- in main, before the call to `Scheduler::start()`, start your task with its code like this :

```cpp
task.start([=]()
	{
		while(true)
		{
			/* SET THE LED ON */
			opsy::sleep_for(250ms);
			/* SET THE LED OFF */
			opsy::sleep_for(250ms);
		}
	});
```

Most tasks do work indefinitely, so they use a while(true) loop, here the task job is to set the LED on, wait 250ms (milliseconds), set the LED off, and wait again 250ms.
We can use the "XXXms" thanks to `std::chrono_literals`, many are defined for seconds, minutes, etc. Run it, now you LED is blinking at 2Hz.

I can do that without an RTOS, you are probably thinking ... right, using "delay" or for loops in replacement of sleep_for, but now comes the good part.
Declare another task, and make it blink another LED, maybe with an extra `sleep_for` before the while loop to make it out of synch with the first one, or use different on or off periods ...
You can now write your project as independant modules, BOTH in terms of memory usage, AND in terms of processing. OpSy allows you to run them as if each one had its own CPU.

Welcome in the whole new world of RTOS !!!!

# History

This version of OpSy is the third main iteration of the RTOS. I started the very first implementation when working on the Neuron Flybarless unit.
It has proven to be very reliable with hundreds of thousands of flight with no issue.
I did a complete redesign when working on the EzManta project, which required more tasks, more complexity, etc.
Then, over the years, I built a list of things I would like it to do, or do better, or differently, and as we became ST Partner, we considered releasing it open source.
With the projects we are now working on, and the new products coming from ST (both hardware and software, be patient ;) ), it was time to work on this third iteration.

# Current Version

Current Version is in alpha state, first implementation is done but requires a lot of testing before it is usable in production environment.