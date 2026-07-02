## The GWToolbox.exe just forced an install on the Windows machine from 8.29 to 8.30 with the configuration set to off and NO pressed multiple times. The Windows pop-up was labeled (v4.7)

We currently build the dll and have a fully featured deploy script with a companion Windows script that is ran over SSH with great success.
Review the GWToolboxpp submodule (a fork of our own, days ago) and architect a new flag for building and deploying our own executable as well, to be placed at X.

## Our Fork is 48 commits behind master
In addition, plan to determine A. Where and how this was forced (it is always to be a user mistake), B. How to protect our own fork and C. Any similarly-concerning capacity of GWToolbox after this signal/canary.
The GWToolbox settings (pointed to somewhere in %AppData% but cannot be found at this time) had been set to not automatically update - it seems likely the there may be some errant leadership/activity in the repo at this point. When 8.30 automatically opened it displayed a pop-up on the gamescreen.
In our fork's state should we be concerned and decouple any further? This is a first; I am not against forking out and leaving main behind - the maintainer himself is the 15th or 16th since beginning.

All in all there is likely nothing to be _that_ concerned about, but I dislike this behavior especially knowing the people they _could_ be taking advantage of - hence the "I would be fine running a better GWToolbox fork from my Mac especially if they popups would stop."

## Another Fork to Consider in our Deploy Script - TBD
The GWLauncher project has been ideated to be added in - it may also be something to fork ourselves in the future (https://github.com/gwdevhub/gwlauncher) which is also under the same hub as GWLauncher at https://github.com/gwdevhub

# Build
Add the feature and flag to build and install the executable launcher GWToolbox.exe on the Windows host.

# Reference
Successful dll log can be found at ./dll-success.log.
