# Server-Side Rain Forecast — Design Questions

## Q: Why does the server truncate precipitation (int(mm)) instead of rounding?

**A:** Asymmetry of harm drives the choice.

| Server reports | Firmware gate (RAIN_FORECAST_Y_MM = 2 mm) | Outcome |
|----------------|-------------------------------------------|---------|
| True 1.5 mm → rounded to 2 mm | 2 ≥ 2 → forecast **accepted** | Overstated rain → skip/hold watering → risk of under-watering |
| True 1.5 mm → truncated to 1 mm | 1 < 2 → forecast **rejected** | Understated rain → full water to threshHigh → at most one Z-capped run of mild overwater |

**Overstated rain** causes skipped/held watering; the plant may not get water it needs.  
**Understated rain** causes full watering; worst case is one Z-capped run of mild overwater.

The asymmetry favors avoiding desiccation over mild overwater. Truncation (int(mm), i.e. floor for non-negative) ensures the server **only ever understates** precipitation, never overstates it. This aligns with the firmware's decision-tree asymmetry where forecast alone can only downgrade full → Z, never skip.