# TruckSim GPS plugin payload

The x64 and x86 plugin DLLs are the official TruckSim GPS telemetry plugin at
`TruckSim-GPS/trucksim-gps-plugin` commit
`ab79d819229740978bb22fb338a2b12bf5f623bf`. They were obtained from that
plugin's official server bundle at commit
`8387319e77c1681df79b313650c193ae9cc5bcc1`. The plugin repository identifies
the DLL as MIT-licensed and identifies its included SCS SDK as MIT-licensed;
the required notices are included beside this file.

The plugin writes a 32 KiB revision-13 map at `Local\TSGPSTelemetry`. NLSI
reads that map directly and does not start the GPL-3.0 server application.
SHA-1 Git blob IDs are x64 `b0b8b0f25b254a72d002ef9e3eb12198ce88a117` and
x86 `63b48b079fde942b3fb52b04e6847daccf5572e9`.
