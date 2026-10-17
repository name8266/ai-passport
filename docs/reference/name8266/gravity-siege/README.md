<p align="right"><strong>English</strong> · <a href="README.zh_CN.md">简体中文</a></p>

# Gravity Siege — Passport Physics Arcade

This is a new standalone application, **not** a reskin of the coin pusher,
pachinko, or the original BSP demonstration menu.

## Gameplay

Launch a heavy orange projectile into towers of turquoise breakable glass
spheres and gray stone spheres. The impact solver transfers momentum through
contact chains. Aim low to strike the base or high to scatter the upper stack.
Destroy every turquoise target within the stage's 5–8 shot allowance to advance to the next stage.
Combos increase the score; a stage clear awards 250 points.

- **UP click:** raise launch angle by 5°
- **DOWN click:** lower launch angle by 5°
- **OK click:** launch / next stage / retry
- **UP long press:** change power (three steps)
- **DOWN long press:** new run, score reset
- **OK long press:** pause or resume

## Physical simulation

`main/kinetic_core.c` implements an independent deterministic circle-body
impulse solver. It simulates gravity, material-dependent restitution, position
correction, Coulomb friction, angular inertia, rolling, wall/floor contact,
and velocity-triggered fracture. Two physics substeps and three contact solver
iterations are performed per 60 Hz game step. No scripted fall or rigged
target outcome is used. Shattering releases additional localized impulse.

- 28 preallocated body slots, with no heap allocation in the physics solver
- Fixed 60 Hz step and 30 FPS **target** display timer
- Deterministic seed for reproducible host simulations
- Targets use 2–4 high rows, increasing challenge variability
- All gameplay offline; no network or account needed

The solver uses **circles**, not general convex polygons, and its force scale
is tuned for arcade gameplay rather than SI-calibrated lab measurements.
Real-device FPS and battery draw remain unmeasured until hardware validation.

## Source and validation

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -Imain \
    tests/test_kinetic_core.c main/kinetic_core.c -lm \
    -o /tmp/gravity-siege-host
/tmp/gravity-siege-host
./tools/validate.sh --static
./tools/validate.sh --firmware  # ESP-IDF 5.5.3 required
```

Flash only a verified merged firmware image at the correct offsets, after
confirming impact on stored device data and obtaining approval. GitHub Actions
on this branch exercises host physics tests and the ESP-IDF firmware gate.
