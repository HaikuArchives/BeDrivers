#!/bin/sh
#
# This scripts deinstalls the AD1816(A) audio driver in BeOS Intel R4.
#

answer=`alert "Do you really want to uninstall the AD1816(A) driver?" "No" "Yes"`
if [ $answer == "No" ]; then
	exit
fi

rm ~/config/add-ons/kernel/drivers/bin/ad1816
rm ~/config/add-ons/kernel/drivers/dev/audio/raw/ad1816
rm ~/config/add-ons/kernel/drivers/dev/audio/old/ad1816
rm ~/config/add-ons/kernel/drivers/dev/audio/mix/ad1816
rm ~/config/add-ons/kernel/drivers/dev/audio/mux/ad1816
rm ~/config/add-ons/kernel/drivers/dev/midi/ad1816

alert "The AD1816(A) has been removed.

Restart your system now."
