#include "xparameters.h"
#include "xgpio.h"

//====================================================
#define LED_CHANNEL 1
int main (void) 
{

   XGpio dip, push, led;
   int i, psb_check, dip_check;
	
   xil_printf("-- Start of the Program --\r\n");
 
   XGpio_Initialize(&dip, XPAR_SWITCHES_DEVICE_ID); // Modify this
   XGpio_SetDataDirection(&dip, 1, 0xffffffff);
	
   XGpio_Initialize(&push, XPAR_BUTTONS_DEVICE_ID); // Modify this
   XGpio_SetDataDirection(&push, 1, 0xffffffff);

   XGpio_Initialize(&led, XPAR_LED_IP_DEVICE_ID); // Modify this
      XGpio_SetDataDirection(&led, 1, 0xffffffff);
	

   while (1)
   {
	  psb_check = XGpio_DiscreteRead(&push, 1);
	  xil_printf("Push Buttons Status %x\r\n", psb_check);
	  dip_check = XGpio_DiscreteRead(&dip, 1);
	  xil_printf("DIP Switch Status %x\r\n", dip_check);
	  
	  // output dip switches value on LED_ip device
	  XGpio_DiscreteWrite(&led, LED_CHANNEL, dip_check);
	  
	  for (i=0; i<9999999; i++);
   }
}
