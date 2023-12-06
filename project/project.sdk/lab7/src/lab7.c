#include "xparameters.h"
#include "xgpio.h"
#include "xscutimer.h"
// Include scutimer header file
#include "xscugic.h"
#include "xil_exception.h"
#include "xil_printf.h"

#define LED_CHANNEL 1
/**************************** Variable Definitions *******************************/
//just want to make program a little easier, many of them can actually be defined as local
XGpio dip, push,led;		/* two GPIO instances for dip switches and push buttons*/
int psb_check, dip_check, dip_check_prev, count;
// PS Timer related definitions
XScuTimer_Config *ConfigPtr;
XScuTimer Timer;		/* Cortex A9 SCU Private Timer Instance */
XScuTimer *TimerInstancePtr = &Timer;

// interrupt related definitions
XScuGic IntcInstance;		/* Interrupt Controller Instance */
XScuGic *IntcInstancePtr = &IntcInstance;
XScuGic_Config *IntcConfig;

volatile int led_count = 0; // set it to be global variable so that the ISR can access it easily.

#define ONE_TENTH 32500000 // half of the CPU clock speed/10

/************************** Function Prototypes ******************************/
static int Init_Peripherals();
static int Init_GIC();
static void TimerIntrHandler(void *CallBackRef);
static void Push_Intr_Handler(void *CallBackRef);
static void Dip_Intr_Handler(void *CallBackRef);

/*****************************************************************************/
/**
*
* This function initialize dip switches, push buttons and timer.
*
* @param	pDip is a pointer to the instance of dip XGpio.
* @param	pPush is a pointer to the instance of switch XGpio.
* @param	pTimer is a pointer to the XScuTimer.
*
* @return	XST_SUCCESS if successful, otherwise XST_FAILURE.
*
* @note		None.
*
******************************************************************************/
static int Init_Peripherals()
{
//   if(!XGpio_Initialize(&dip, XPAR_SWITCHES_DEVICE_ID))
//   {
//		xil_printf("Dip switch init failed\r\n");
//		return XST_FAILURE;
//   }
//   XGpio_SetDataDirection(&dip, 1, 0xffffffff);

   int xResult = XGpio_Initialize(&dip, XPAR_SWITCHES_DEVICE_ID);
   if (xResult != XST_SUCCESS){
		xil_printf("Dip switch init failed\r\n");
		return XST_FAILURE;
   }
   XGpio_SetDataDirection(&dip, 1, 0xffffffff);

   xResult = XGpio_Initialize(&push, XPAR_BUTTONS_DEVICE_ID);
   if (xResult != XST_SUCCESS){
	   xil_printf("Push buttone init failed\r\n");
	   return XST_FAILURE;
   }
   XGpio_SetDataDirection(&push, 1, 0xffffffff);

   xResult = XGpio_Initialize(&led, XPAR_LED_IP_DEVICE_ID);
      if (xResult != XST_SUCCESS){
   	   xil_printf("Push buttone init failed\r\n");
   	   return XST_FAILURE;
      }
      XGpio_SetDataDirection(&led, 1, 0xffffffff);

   // Initialize the timer
   ConfigPtr = XScuTimer_LookupConfig (XPAR_PS7_SCUTIMER_0_DEVICE_ID);
   xResult = XScuTimer_CfgInitialize(&Timer, ConfigPtr, ConfigPtr->BaseAddr);
   if (xResult != XST_SUCCESS){
	  xil_printf("Timer init() failed\r\n");
	  return XST_FAILURE;
   }

   return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function initialize interrupt controller
*
* @return	XST_SUCCESS if successful, otherwise XST_FAILURE.
*
* @note		None.
*
******************************************************************************/
static int Init_GIC()
{
	//Interrupt Controller initialization
	IntcConfig = XScuGic_LookupConfig(XPAR_SCUGIC_0_DEVICE_ID);
	if(!IntcConfig)
	{
	   xil_printf("Looking for GIC failed! \r\n");
	   return XST_FAILURE;
	}

	int xResult = XScuGic_CfgInitialize(IntcInstancePtr, IntcConfig, IntcConfig->CpuBaseAddress);
	if(xResult != XST_SUCCESS)
	{
	   xil_printf("Init GIC failed! \r\n");
	   return XST_FAILURE;
	}

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function connect GPIOes to GIC and configure GIC so that interrupts can occur
*
* @return	XST_SUCCESS if successful, otherwise XST_FAILURE.
*
* @note		None.
*
******************************************************************************/
static int Configure_GIC()
{
	//Interrupt Priority and Trigger Setup for dip switches
	XScuGic_SetPriorityTriggerType(IntcInstancePtr, XPAR_FABRIC_SWITCHES_IP2INTC_IRPT_INTR, 0xA0, 0x3);
	//Connect GPIO Interrupt to handler dipswitch
	int xResult = XScuGic_Connect(IntcInstancePtr,
							XPAR_FABRIC_SWITCHES_IP2INTC_IRPT_INTR,
							(Xil_ExceptionHandler)Dip_Intr_Handler,
							(void *)&dip);
	if(xResult != XST_SUCCESS)
	{
	   xil_printf("Connect dip switch interrupt failed! \r\n");
	   return XST_FAILURE;
	}

	//Interrupt Priority and Trigger Setup for push button
	XScuGic_SetPriorityTriggerType(IntcInstancePtr, XPAR_FABRIC_BUTTONS_IP2INTC_IRPT_INTR, 0x8, 0x3);
	//Connect GPIO Interrupt to handler for push button
	xResult = XScuGic_Connect(IntcInstancePtr,
							XPAR_FABRIC_BUTTONS_IP2INTC_IRPT_INTR,
							(Xil_ExceptionHandler)Push_Intr_Handler,
							(void *)&push);
	if(xResult != XST_SUCCESS)
	{
	   xil_printf("Connect push button interrupt failed! \r\n");
	   return XST_FAILURE;
	}


	//Interrupt Priority and Trigger Setup for timer
	XScuGic_SetPriorityTriggerType(IntcInstancePtr, XPAR_SCUTIMER_INTR, 0xA8, 0x3);
	/*
	 * Connect the interrupt handler that will be called when an
	 * interrupt for the timer occurs
	 */
	xResult = XScuGic_Connect(IntcInstancePtr, XPAR_SCUTIMER_INTR,
				(Xil_ExceptionHandler)TimerIntrHandler,
				(void *)TimerInstancePtr);
	if(xResult != XST_SUCCESS){
	   xil_printf("Connect timer interrupt failed! \r\n");
	   return XST_FAILURE;
	}

	return XST_SUCCESS;
}



/*****************************************************************************/
/**
*
* This function enables the interrupts at the GIC and GPIOs
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void EnableInts()
{
	/*
	 * Enable the interrupt for devices
	 */
   //Enable GPIO and Timer Interrupts in the controller
   XScuGic_Enable(IntcInstancePtr, XPAR_FABRIC_SWITCHES_IP2INTC_IRPT_INTR);
   XScuGic_Enable(IntcInstancePtr, XPAR_FABRIC_BUTTONS_IP2INTC_IRPT_INTR);
   XScuGic_Enable(IntcInstancePtr, XPAR_SCUTIMER_INTR);

 //Enable CPU Interface Control Register
    /** @name Control Register
     * CPU Interface Control register definitions
     * All bits are defined here although some are not available in the non-secure
     * mode.
     * @{
     */
 //   #define XSCUGIC_CNTR_SBPR_MASK	0x00000010U    /**< Secure Binary Pointer,
 //                                                    0=separate registers,
 //                                                    1=both use bin_pt_s */
 //   #define XSCUGIC_CNTR_FIQEN_MASK	0x00000008U    /**< Use nFIQ_C for secure
 //                                                     interrupts,
 //                                                     0= use IRQ for both,
 //                                                     1=Use FIQ for secure, IRQ for non*/
 //   #define XSCUGIC_CNTR_ACKCTL_MASK	0x00000004U    /**< Ack control for secure or non secure */
 //   #define XSCUGIC_CNTR_EN_NS_MASK		0x00000002U    /**< Non Secure enable */
 //   #define XSCUGIC_CNTR_EN_S_MASK		0x00000001U    /**< Secure enable, 0=Disabled, 1=Enabled */
   XScuGic_CPUWriteReg(IntcInstancePtr, 0x0, 0xf);

   //Enable GPIO Interrupts Interrupt
	XGpio_InterruptEnable(&dip, 0xF);
    XGpio_InterruptGlobalEnable(&dip);

	XGpio_InterruptEnable(&push, 0xF);
    XGpio_InterruptGlobalEnable(&push);

   /*
	 * Enable the timer interrupts for timer mode.
	 */
	XScuTimer_EnableInterrupt(TimerInstancePtr);
}

static void ExceptionInit()
{
	Xil_ExceptionInit();

// When an interrupt occurs, the processor first has to interrogate the interrupt
//	controller to find out which peripheral generated the interrupt which is done by
//  �XScuGic_InterruptHandler�.
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT,
							(Xil_ExceptionHandler)XScuGic_InterruptHandler,
							IntcInstancePtr);

	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_FIQ_INT,
			(Xil_ExceptionHandler) XScuGic_InterruptHandler,
			(void *)IntcInstancePtr);

	//Xil_ExceptionEnable();
	Xil_ExceptionEnableMask(XIL_EXCEPTION_ALL);
}

static void Push_Intr_Handler(void *CallBackRef)
{
	XGpio *push_ptr = (XGpio *) CallBackRef;

	xil_printf("Insider Button ISR! ... \r\n");

	// Disable GPIO interrupts
	XGpio_InterruptDisable(push_ptr, 0xF);
	// Ignore additional button presses
//	if ((XGpio_InterruptGetStatus(push_ptr) & 0xFF) !=
//			INTR_MASK) {
//			return;
//		}

//	int psb_check = XGpio_DiscreteRead(&push, 1);
	led_count = 0xAAAA;

	xil_printf("Here again ... \r\n");

	XGpio_DiscreteWrite(&led, LED_CHANNEL, led_count);

    (void)XGpio_InterruptClear(&push, 0xF);
    // Enable GPIO interrupts
    XGpio_InterruptEnable(&push, 0xF);
}


static void Dip_Intr_Handler(void *CallBackRef)
{
	XGpio *dip_ptr = (XGpio *) CallBackRef;

	xil_printf("Insider Switch ISR! Ha Ha Ha Ha Ha Ha Ha Ha... \r\n");

	// Disable GPIO interrupts
	// not sure how to setup interrupt mask yet ...
	XGpio_InterruptDisable(dip_ptr, 0xF);
	// Ignore additional button presses (not sure about this function yet)
//	if ((XGpio_InterruptGetStatus(dip_ptr) & INTR_MASK) !=
//			INTR_MASK) {
//			return;
//		}

	int dip_check = XGpio_DiscreteRead(dip_ptr, 1);
	led_count = dip_check;
	XGpio_DiscreteWrite(&led, LED_CHANNEL, led_count);

    (void)XGpio_InterruptClear(dip_ptr, 0xF);
    // Enable GPIO interrupts
    XGpio_InterruptEnable(dip_ptr, 0xF);
}

/*****************************************************************************/
/**
*
* This function is the Interrupt handler for the Timer interrupt of the
* Timer device. It is called on the expiration of the timer counter in
* interrupt context.
*
* @param	CallBackRef is a pointer to the callback function.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void TimerIntrHandler(void *CallBackRef)
{
	XScuTimer *TimerInstancePtr = (XScuTimer *) CallBackRef;

	/*
	 * Check if the timer counter has expired, checking is not necessary
	 * since that's the reason this function is executed, this just shows
	 * how the callback reference can be used as a pointer to the instance
	 * of the timer counter that expired, increment a shared variable so
	 * the main thread of execution can see the timer expired.
	 */
	xil_printf("Insider Timer ISR! ... \r\n");

	if (XScuTimer_IsExpired(TimerInstancePtr)) {
		XScuTimer_ClearInterruptStatus(TimerInstancePtr);
		XGpio_DiscreteWrite(&led, LED_CHANNEL, led_count);
		led_count++;
//		if (TimerExpired == 3) {
//			XScuTimer_DisableAutoReload(TimerInstancePtr);
//		}
	}
}

/*****************************************************************************/
/**
*
* This function disables the interrupts that occur for the device.
*
* @param	IntcInstancePtr is the pointer to the instance of XScuGic
*		driver.
* @param	TimerIntrId is the Interrupt Id for the device.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void TimerDisableIntrSystem(XScuGic *IntcInstPtr, u16 TimerIntrId)
{
	/*
	 * Disconnect and disable the interrupt for the Timer.
	 */
	XScuGic_Disconnect(IntcInstPtr, TimerIntrId);
}

/******************************************************************************/
/**
*
* This function disables and disconnects the interrupt system
*
*
* @return	None
*
* @note		None.
*
******************************************************************************/
void DisableIntr()
{
	/*
	 * Disable and disconnect the interrupt system.
	 */
	TimerDisableIntrSystem(IntcInstancePtr, XPAR_SCUTIMER_INTR);

	XGpio_InterruptDisable(&dip, 0x3);
	XGpio_InterruptDisable(&push, 0x3);

	/* Disconnect the interrupt */
	XScuGic_Disable(IntcInstancePtr, XPAR_FABRIC_SWITCHES_IP2INTC_IRPT_INTR);
	XScuGic_Disconnect(IntcInstancePtr, XPAR_FABRIC_SWITCHES_IP2INTC_IRPT_INTR);
	XScuGic_Disable(IntcInstancePtr, XPAR_FABRIC_BUTTONS_IP2INTC_IRPT_INTR);
	XScuGic_Disconnect(IntcInstancePtr, XPAR_FABRIC_BUTTONS_IP2INTC_IRPT_INTR);
}


int main (void)
{

   xil_printf("-- Start of the Program --\r\n");

   count = 0;

   Init_Peripherals();

   xil_printf("Initialization GPIO and timer done !\r\n");

   Init_GIC();

   xil_printf("Init GIC done !\r\n");

   Configure_GIC();

   xil_printf("Configure GIC done !\r\n");

   EnableInts();
   xil_printf("Enable Interrupts done !\r\n");

   ExceptionInit();

   xil_printf("Initialization done !\r\n");

   // Start the timer
   // Read dip switch values
   dip_check_prev = XGpio_DiscreteRead(&dip, 1);
   // Load timer with delay in multiple of ONE_TENTH
   //XScuTimer_LoadTimer(&Timer, ONE_TENTH*dip_check_prev);
   XScuTimer_LoadTimer(&Timer, ONE_TENTH*dip_check_prev);
   // Set AutoLoad mode
   XScuTimer_EnableAutoReload(&Timer);
   // Start the timer
   XScuTimer_Start (&Timer);

   xil_printf("Timer on !\r\n");

   while (1)
   {
	  // Read push buttons and break the loop if Center button pressed
	  psb_check = XGpio_DiscreteRead(&push, 1);
	  if(psb_check > 0)
		  	 xil_printf("Push button Status %x\r\n", psb_check);

	  if(psb_check == 8)
	  {
		  xil_printf("Push button pressed: Exiting\r\n");
		  XScuTimer_Stop(TimerInstancePtr);
		  break;
	  }
	  dip_check = XGpio_DiscreteRead(&dip, 1);
	  if (dip_check != dip_check_prev) {
		  xil_printf("DIP Switch Status %x, %x\r\n", dip_check_prev, dip_check);
		  dip_check_prev = dip_check;
	  	  // load timer with the new switch settings
		  XScuTimer_LoadTimer(TimerInstancePtr, ONE_TENTH*dip_check);
		  count = 0;
	  }
	  if(XScuTimer_IsExpired(TimerInstancePtr)) {
			  // clear status bit
		  	  XScuTimer_ClearInterruptStatus(TimerInstancePtr);

		  	  // output the count to LED and increment the count
		  	XGpio_DiscreteWrite(&led, LED_CHANNEL, led_count);
		  	  count++;
	  }
   }

   DisableIntr();

   return 0;
}
