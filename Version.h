// Keep track version numbers
#ifndef _VERSION
#define _VERSION

// Not sure if it's catchy, but at least it points to extra data on the dashboard :-)
const char* g_ProjectName = "DataDash+";

// Very simple versioning, i.e. no need for tracking major/minor numbers
//const float g_Version = 1.0f  // Initial
//const float g_Version = 1.1f; // Add engine temp, name and versioning
//const float g_Version = 1.2f; // Use Media "channel" for front USB to send messages to avoid radio station info to interfere with custom messages
//const float g_Version = 1.3f; // Fix text flickering and display sometimes freezing, by resetting custom text sequences when frames from radio is observed
//const float g_Version = 1.4f; // Reduce CAN bus traffic by only resetting custom text sequences when seeing the *last* radio frame, not each one
const float g_Version   = 1.5f; // Add turbo cooldown timer and reset custom text sequences already on the 2nd last radio frame

#endif