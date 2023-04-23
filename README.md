Requiered resources:
1: Quartus II version 13.0 or above
2: Nios II Software Build Tools (Eclipse)
3: DE2-115 development board and cables (USB and power)
4: PS/2 Keyboard
5: VGA to HDMI adapter
6: HDMI cable

Connection Guide:
1.Use the power cable to power on DE2 board. 
2.Connect the DE2 blaster port with PC by using USB cable.
3.Connect the DE2 VGA port with the VGA2HDMI adapter, make sure the usb cable of the adapter connect to 4.a power source. Use HDMI cable connect the adapter with a monitor.
5.Connect the DE2 PS2 port with keyboard.


Set up Quatus2:
Unzip the compsys723-main, you will find 'freq_relay_controller.sof' inside the labtest1 folder.
Open quatus2 , Tools-> Programmer. Click on hardware setup and choose 'USB-Blaster'. 
If you cannot find USB blaster, that might be driver issue: windows need to update the driver
to make the USB Blaster work, please follow this website to update the driver:
https://www.terasic.com.tw/wiki/Altera_USB_Blaster_Driver_Installation_Instructions
Click on 'Add File' and select 'freq_relay_controller.sof' under the labtest1 folder. Make sure the program/configure tick box is on, and all other tick boxes are off. Then click on 'start'. If the progress failed, you need to click 'Start' once more until it shows successful, or power off and power on the DE2 board.

Set up Nios2:
Open Nios II Software Build Tools (Eclipse), it will require you to specific the work space, please choose the 'labtest1' folder as the workspace. It will load the project. All you need to do is build 'freerots_test' the project. Then click on 'Run' button.


Constants:
in the 'FreeRTOS_freq_plot.c' source file, there are two gloable variables. 
double freqThresh = 49.00;
double ROCThresh = 10.00;
These are the initial frequency and RoC thresholds. You can modify them to other values. 

When the code is running, if you want to change frequency threshold, press 'f' on the keyboard, then input numbers by pressing keypad, when you finished input numbers, press 'enter'.
if you want to change the frequency threshold to '48.56',please press follow keys on the ps2 keyboard(do not press comma): 'f','4','8','.','5','6','Enter'. 'r' key is for changing RoC threshold, so if you want to change RoC to '10.23'. Press following keys: 'r','1','0','.','2','3','enter'. Please use the keypad to input numbers instead of the numbers above letter keys.

If you want to go maintain mode, press and hold the 'key03' push button on DE2 board. After the push button03 released, it will back to normal working mode immediately, so please press and hold it.
Under maintain mode, frequency relay will not shed any load, only the switch button controls the load.


