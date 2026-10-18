# Free-roam audit: driving / vehicle physics (2026-10-14)

Sources: spec-physics-runtime.md (10.x, 10.11, 10a), spec-vehicle-damage-runtime.md, code in include/sr3physics, src/physics_vehicle_*.cpp, tools/sr3_viewer.cpp (PlaytestVehicleRig).
Model: planar 13-sub-step simulator (`VehicleSubstepSimulator`) with CHOSEN stand-in tyre/load model; no vertical state.

Status key: IMPL = implemented and wired into the playtest path; PART = partly / pure code exists but not wired or stand-in involved; MISS = nothing.

| # | Area (spec) | Status | Where / note |
|---|---|---|---|
| 1 | Engine torque curve, 9-field crossed mapping (10.2) | IMPL | `EngineNetTorque`, `VehicleTuning::ToEngineCurveTuning`. Final torque scale global `0x02845af4` OPEN, CHOSEN 1.0 |
| 2 | Gears / shift state machine / downshift RPMs / diff (10.3, 10.10) | IMPL | `GearShiftStateMachine`, `BuildGearShiftTuning`. Reverse gear and manual shift not modelled (OPEN in spec) |
| 3 | Throttle response curve (10.6) | IMPL | `ApplyThrottleResponseCurve` in Step01 |
| 4 | Brakes torque law (10.6, 10.11.8) | PART | `ComputeBrakeTorque` wired; per-axle `Wheel_Braking_Torque`; Min_Pedal_To_Block / handbrake per axle (`Wheel_Does_Handbrake`) not used |
| 5 | Steering: input->angle, inverse-square speed falloff (10.1) | IMPL | `SteeringBaseAngle`; speed term is forward speed (spec projection detail OPEN) |
| 6 | Steering rate limiter, V and A=V^2/2d profile (10.11.7) | PART | rates now from data (Steering_Wheel_Max_Speed / return speed); limiter has no separate damp-angle acceleration; helper `SteeringLimiterAccel`/`SteeringFullLockTimeSec` tested, unwired |
| 7 | Tyre slip velocity (10.8) | IMPL | `ComputeTyreSlipVelocity` in ProcessAxle |
| 8 | Friction ellipse saturation (10.8) | PART | `SolveFrictionEllipse` wired with CHOSEN no-curve default |
| 9 | 16-entry friction table values (10.11.6, FINAL) | PART | NEW `BuildFrictionSlipTableFromSpec` + `FrictionRatioX`, tested vs spec genki/exotic vectors. Not installed: real lookup/interpolation rule is OPEN |
| 10 | Wheel friction / max friction / viscosity (10.11.6) | PART | Wheel_Friction, Wheel_Max_Friction used; Viscosity_Friction, Static_Load_Friction (equalizer), drift modifier unused |
| 11 | Suspension spring-damper formula (10.4) | PART | `SuspensionSpringDamperForceNoXDenominator` pure but sim has no vertical state, so unwired; X denominator = Mass now known (10.11.5): NEW `PerMassToPerWheel` |
| 12 | Static sag / ride height (10.11.5, FINAL formula) | PART | NEW `StaticSagFront/Rear` tested (genki, pickup); unwired (needs vertical model) |
| 13 | Wheel geometry (radius, wheelbase, track) (10.11.4) | PART | NEW `WheelRadiusFromSizeIndex` tested; playtest genki rig now uses spec-table values (CHOSEN-free for genki); generic per-vehicle derivation from .ccar_pc parts not wired |
| 14 | Wheel collide / raycast (10.5) | PART | AABB raycast pure; world collision is a bounding-sphere stand-in (vehicle_world_collision.h), no per-wheel ground query |
| 15 | Mass / chassis inertia (10.11.2) | IMPL | mass; yaw inertia = Mass x Yaw_Unit_Inertia now in `FromVehicleEntry` (NEW). Pitch/roll absent (planar) |
| 16 | Torque-factor friction inertia (10.11.3) | MISS | only yaw plane; roll/pitch factors unused |
| 17 | Aerodynamic drag (10.11.8) | IMPL | Step13-adjacent drag with data Cd/A/rho |
| 18 | Aerodynamic lift / downforce (10.11.8, FINAL) | PART | NEW `LiftForceN` + `liftCoefficient` field; planar model has no normal-load input from it yet |
| 19 | Velocity (spin) damper (10.11.8, FINAL) | IMPL (new) | `ApplyVelocityDamper` wired in IntegrateChassis |
| 20 | Maximum_Speed hard clamp (10.11.10, FINAL) | IMPL (new) | `ClampPlanarSpeed` wired in IntegrateChassis. Nitrous blend / bonus term inputs OPEN |
| 21 | Extra-gravity action (10.11.8) | MISS | enable predicate OPEN; no vertical model |
| 22 | Component slot map (10.11.0) | PART | spec map is final; vehicle_substeps.h comments still carry the old wrong guess |
| 23 | Contact-listener collision detect/response (10a) | PART | `vehicle_world_collision.h` sphere stand-in; no impulse model from 10a |
| 24 | Vehicle damage (spec-vehicle-damage-runtime.md) | MISS (in sr3physics) | not part of the driving loop; audit of that spec is outside this pass |
| 25 | Real per-vehicle data in playtest | PART | playtest rig only ever spawns car_4dr_genki; spec genki values hardcoded (labelled). No xtbl lookup by vehicle name in the playtest loop |

Counts (25 rows): IMPL 9, PART 13, MISS 3.

Top remaining gaps: (a) no vertical / suspension / roll-pitch dynamics so rows 11, 12, 16, 18, 21 stay unwired; (b) friction table lookup rule OPEN (row 9); (c) per-vehicle xtbl + .ccar_pc geometry resolution in the playtest (row 13, 25); (d) collision response (row 23); (e) handbrake, reverse gear, Min_Pedal_To_Block.
