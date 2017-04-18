# raspir
Raspberry PI IR Remote Control

A generic IR remote control reading a timing file and sending a sequence on GPIO output pin to IR diode.

Can sample any IR remote control if hardware available. 

Sampling tested using only two cables directly connected between remote control IR led and Raspberry PI GPIO input pin. 

Sending tested using only 33 Ohm resistor and IR diode connected in serie to GPIO output pin.

Sampling and sending has been tested and working with Mitsubishi Electric Heatpump MSZ-FH35VE

See github [wiki](https://github.com/ultrapi/raspir/wiki) for more info
