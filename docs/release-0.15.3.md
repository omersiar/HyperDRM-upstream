# HyperDRM 0.15.3

Fix the remote-control focus order in the TV calibration panel.

`setupRemoteNavigation()` walks a fixed list, and the new **Geometry** button was
placed before `Save profile` and `Edit profile JSON` in that list while rendering
after both. Arrow keys therefore jumped `Profile` -> `Geometry` -> `Save profile`
-> `Edit profile JSON`, which does not follow the on-screen row.

The list now matches the button order, so left/right and up/down step through
`Profile`, `Save profile`, `Edit profile JSON`, `Geometry` as displayed. A test
compares both the navigation list and `index.html` button order for those four
controls, and a check over the full list confirms no other panel is affected.
