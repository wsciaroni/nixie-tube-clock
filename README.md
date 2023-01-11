# nixie-tube-clock
Work in progress for designing and building a custom Nixie Tube Clock

## Starting Point

This project is incomplete.

I have started from the really helpful blog here: https://crystal.uta.edu/~burns/project_nixieclock.html

## Overview

I remember my first exposure to Nixie Tubes vividly. It was an old coin counting machine we were using for a change colection fund raiser. I was fascinated by the Nixie Tubes and have never forgotten them.

After observing many other Nixie Clock builds, I decided there wasn't one that met all of my needs.

The closest that I found was here: https://crystal.uta.edu/~burns/project_nixieclock.html. However, I would like a more accurate time. So, I plan to start with this project and modify it.  My goal is to

- Remove the LEDs (I don't really want a backlight)
- Change the embedded system to an ESP8266 / NodeMCU with WiFi to maintain time with the network
- Make sure the programming port is accessible so that I can reprogram or update the firmwware as needed

My Requirements for the clock are as follows

1. The clock needs to use Nixie Tubes
2. The Clock would sync with WiFi to keep a very accurate time
3. The high voltage supply's circuitry will be it's own board for modularity
4. It will be a 6 digit 24-hour clock (H1 H0 : M1 M0 : S1 S0)
5. The clock should be powered via 120V AC

