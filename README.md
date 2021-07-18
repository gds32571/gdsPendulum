# gdsPendulum

 My project to monitor & control a grandfather clock built for me by my father.  It's been running
  for many years and is starting to show wear in the timekeeping mechanism and the chimes.
  
  I use an arduino with an electromagnet to influence the clock to maintain the correct time within
  less than a second relative to a Adafruit Ultimate GPS Breakout GPS. Typically, the clock drifts within a 100 millisecond range over weeks.
  
Picture of clock 

https://www.flickr.com/photos/88117211@N00/33824316033/

Picture of controller

https://www.flickr.com/photos/88117211@N00/51009642002/

Statistics graph:

https://www.flickr.com/photos/88117211@N00/51009567372/


### 16 Jul 2021

Removed apt-get of arduino and downloaded version from arduino people. This lets me compile and upload on the actual rp1. I am happy about that!
 
Goto  https://www.arduino.cc/en/software  and download the ARM version, maybe 

https://downloads.arduino.cc/arduino-nightly-linuxarm.tar.xz

Note to self: remember to pushit from dell2 in //rp1/pi/Arduino/sketchbook/gdsPendulum folder, not from rp1.
