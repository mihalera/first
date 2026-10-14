// =============================================================================
//  The two FactoryPresets entry points the processor's constructor reaches from
//  ABOVE the table's definition.
//
//  This is the forward declaration that used to sit in the middle of
//  PluginProcessor.cpp, and it is a header because the table it promises is now
//  defined in another translation unit (Source/ProcessorPresets.cpp). The debug
//  audit inside the constructor is the only code that calls them from outside that
//  file - and it is inside `#if DEBUG`, which is how the missing declaration this
//  block exists to fix stayed invisible: every release build compiled the mistake
//  out of existence.
// =============================================================================
#pragma once

//==============================================================================
//  The factory presets are defined further down this file, in their own
//  namespace. The debug audit inside the constructor below is the only code
//  that reaches them from above that point, so the two entry points it uses
//  are declared here.
//
//  They were previously called with no declaration at all, which is a name
//  lookup failure - but only in a debug build, because the audit sits inside
//  #if DEBUG. Every release build compiles the mistake out of existence, so
//  nothing else in the build could have caught it.
//==============================================================================
namespace FactoryPresets
{
    inline int count();
    inline bool isSentinelValue (float value) noexcept;
}
