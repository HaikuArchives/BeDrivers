C-Media CMI8338/8738-based sound card device driver
version 1.3.2 beta

Marko Koscak < http://beos.tisno.de, beos@tisno.de>
July 17, 2001 - public release

About:

This is the sound device driver for the CMI8x38 chip from C-Media. This chip is preferably used on some PCI cards, like the Winfast 4xSound, and for on-board sound. This audio driver is originally from C-Media. However it was supporting only the analog output and inputs of the sound chip. The digital S/PDIF interfaces could not be used under BeOS in the past.

With this version I added a few features to the original driver and there is now a config tool, too. Currently you can use:

- S/PDIF-in to S/PDIF-out (S/PDIF loop, this routes S/PDIF-in to S/PDIF-out directly, thus it is possible to record digital from a CD/DVD-ROM to MiniDisc, the digital-out of the drive must be attached to the electric S/PDIF-input of the sound card)
- 4 channels analog duplicate mode on 4 jack configuration (i.e. you can hear sound from rear speakers together with the front ones, this is like you use a Y-patch cable)
- Invert S/PDIF-in signal (this is necessary e.g. for my DVD-ROM)
- Monitoring S/PDIF-in signal to Line-out (if necessary the S/PDIF signal have to be inverted, thus the tone at the analogue output sounds different)
- and many more... (look into the history)

To install this driver run the install script. An old cmpci driver will be backuped (File will be renamed to cmpci~).
Copy the cmi8x38 config tool wherever you want.
You should have liblayout installed for the config tool.

This version is still in beta stage. If you have problems with this driver, then please copy the original driver of C-Media back. You find the original driver on the homepage of C-Media (http://www.cmedia.com.tw/) or on bebits.com (http://www.bebits.com/app/1775/).

The cmi8x38 config tool is using liblayout from Marco Nelissen and URLView from William Kakes.

Many thanks to:
Oliver Kuechemann,
Christopher Tate
and Peter

Future plans:

PCM-output over S/PDIF-out and recording over S/PDIF-in.

The following features are not tested yet, it would be great to receive some feedback about that:
- SPDIF-IN/OUT copyright protection
- SPDIF-IN validity detection
- SPDIF-OUT level (Has anyone a device with coaxial input to test it?)

Known bugs:

Status of "SPDIF-IN loopback to SPDIF-OUT" is not correctly received from the driver. Bit seems to be always set. (???)
The same occurs with the SPDIF-IN validity detection and with SPDIF-OUT level status.

Version History:

1.3.2 beta: (Jul 17, 2001) public
		cmpci:
		- pcm_control_hook adapted 
		cmi8x38 config tool:
		- SPDIF-IN/OUT copyright protection implemented, not tested yet
		- SPDIF-IN validity detection implemented, not tested, initial status might be wrong
		- SPDIF-OUT level implemented, but can't test it (don't have a device with coaxial input)
		- SPDIF-IN format bug fixed (you should always read your code twice ;-))

1.3.1 beta: (Jul 12, 2001) internal
		cmpci:
		- pcm_control_hook adapted for SPDIF i/o usage
		cmi8x38 config tool:
		- Settings and Info tab
		- SPDIF and Analog Box build
		- URLView adapted to liblayout
		- 4 channel switch is working
		- Monitor SPDIF-IN is working
		- SPDIF loopback is working, but not correctly initialized
		- SPDIF-IN format does not work, once set in set_default_registers() it only "mutes" output, when switched to the other format.

1.3.0 beta: (Jul 4, 2001) first public releace
		- SPDIF loopback and 4 channels hardcoded
