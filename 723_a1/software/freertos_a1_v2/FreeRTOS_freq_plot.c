#include <stdio.h>
#include <stdlib.h>
#include "sys/alt_irq.h"
#include <system.h>
#include "io.h"
#include <math.h>
#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>

#include "altera_up_avalon_video_character_buffer_with_dma.h"
#include "altera_up_avalon_video_pixel_buffer_dma.h"

#include "altera_up_avalon_ps2.h"
#include "altera_up_ps2_keyboard.h"

#include "altera_avalon_pio_regs.h"
#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/task.h"
#include "FreeRTOS/timers.h"
#include "FreeRTOS/queue.h"






//For frequency plot
#define FREQPLT_ORI_X 101		//x axis pixel position at the plot origin
#define FREQPLT_GRID_SIZE_X 5	//pixel separation in the x axis between two data points
#define FREQPLT_ORI_Y 199.0		//y axis pixel position at the plot origin
#define FREQPLT_FREQ_RES 20.0	//number of pixels per Hz (y axis scale)

#define ROCPLT_ORI_X 101
#define ROCPLT_GRID_SIZE_X 5
#define ROCPLT_ORI_Y 259.0
#define ROCPLT_ROC_RES 0.5		//number of pixels per Hz/s (y axis scale)

#define MIN_FREQ 45.0 //minimum frequency to draw

#define PRVGADraw_Task_P      (tskIDLE_PRIORITY+1)
//#define PRINT_STATUS_TASK_PRIORITY (tskIDLE_PRIORITY+2)
#define KEYBOARD_TASK_PRIORITY (tskIDLE_PRIORITY+2)
#define STABLE_MONITOR_TASK_P (tskIDLE_PRIORITY+5)
#define LOAD_CONTROL_TASK_P (tskIDLE_PRIORITY+6)
#define SWITCH_POLLING_TASK_P (tskIDLE_PRIORITY+3)


TaskHandle_t PRVGADraw;
TaskHandle_t keyboardTaskHandle;
TaskHandle_t stableTaskHandle;
TaskHandle_t loadCtlTaskHandle;
TaskHandle_t switchPollingTaskHandle;
BaseType_t checkIfFieldRequired;
static QueueHandle_t Q_freq_data;


double temp = 50.00;
double currentROC;
double freqThresh = 49.00;
double ROCThresh = 10.00;
char freqStr[10] ="50.00";
char testStr[30];
char oddStr[10];
char ROCStr[10]= "2";
int push_button = 0;
int sw_result;
int sw_result_bin[18];
int current_sw_size;
double previousFreq = 50.00;
double freq[100], dfreq[100];
uint8_t keyboardMode = 0;
unsigned char byte;
unsigned char previousbyte;

double timer1Count = 0.0;
int numOfShed = 0;
bool loadManage =false;
TimerHandle_t timer;
TimerHandle_t timer1;
TimerHandle_t Timer_Reset;

bool mantainMode = false;
bool unstableRemain = false;

// variables to store measurements
int first_shed_load = 0;
int recent_five_sheds[5] = {0};
int min_time = 0;
int max_time = 0;
int avg_time = 0;
int total_time = 0;



int* shedLoads(int numOfShed,int sw_result_bin[]);
int binaryToDecimal(int sw_result_bin[]);
void vTimerCallback(xTimerHandle t_timer);

//this array map the ps2 scan code of the keypad numbers,{0,1,2,3,4.....}
const int numbers[] ={112,105,114,122,107,115,116,108,117,125,113};


typedef struct{
	unsigned int x1;
	unsigned int y1;
	unsigned int x2;
	unsigned int y2;
}Line;

int timerCount = 0;


/****** VGA display ******/

void PRVGADraw_Task(void *pvParameters ){


	//initialize VGA controllers
	alt_up_pixel_buffer_dma_dev *pixel_buf;
	pixel_buf = alt_up_pixel_buffer_dma_open_dev(VIDEO_PIXEL_BUFFER_DMA_NAME);
	if(pixel_buf == NULL){
		printf("can't find pixel buffer device\n");
	}
	alt_up_pixel_buffer_dma_clear_screen(pixel_buf, 0);

	alt_up_char_buffer_dev *char_buf;
	char_buf = alt_up_char_buffer_open_dev("/dev/video_character_buffer_with_dma");
	if(char_buf == NULL){
		printf("can't find char buffer device\n");
	}
	alt_up_char_buffer_clear(char_buf);



	//Set up plot axes
	alt_up_pixel_buffer_dma_draw_hline(pixel_buf, 100, 590, 200, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);
	alt_up_pixel_buffer_dma_draw_hline(pixel_buf, 100, 590, 300, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);
	alt_up_pixel_buffer_dma_draw_vline(pixel_buf, 100, 50, 200, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);
	alt_up_pixel_buffer_dma_draw_vline(pixel_buf, 100, 220, 300, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);

	alt_up_char_buffer_string(char_buf, "Frequency(Hz)", 4, 4);
	alt_up_char_buffer_string(char_buf, "52", 10, 7);
	alt_up_char_buffer_string(char_buf, "50", 10, 12);
	alt_up_char_buffer_string(char_buf, "48", 10, 17);
	alt_up_char_buffer_string(char_buf, "46", 10, 22);

	alt_up_char_buffer_string(char_buf, "df/dt(Hz/s)", 4, 26);
	alt_up_char_buffer_string(char_buf, "60", 10, 28);
	alt_up_char_buffer_string(char_buf, "30", 10, 30);
	alt_up_char_buffer_string(char_buf, "0", 10, 32);
	alt_up_char_buffer_string(char_buf, "-30", 9, 34);
	alt_up_char_buffer_string(char_buf, "-60", 9, 36);

	alt_up_char_buffer_string(char_buf, "Frequency threshold:", 4, 46);


	alt_up_char_buffer_string(char_buf, "RoC threshold:", 4, 50);



	int i = 99, j = 0;
	Line line_freq, line_roc;

	while(1){

		//receive frequency data from queue
		while(uxQueueMessagesWaiting( Q_freq_data ) != 0){
			xQueueReceive( Q_freq_data, freq+i, 0 );

			sprintf(freqStr, "%f", freqThresh);
			sprintf(ROCStr, "%f", ROCThresh);
			alt_up_char_buffer_string(char_buf, ROCStr, 30, 50);
			alt_up_char_buffer_string(char_buf, freqStr, 30, 46);
			if(mantainMode == true){

				alt_up_char_buffer_string(char_buf, "Under Maintain Mode", 30, 55);

			}
			if(mantainMode == false){

				alt_up_char_buffer_string(char_buf, "Under Normal Mode", 30, 55);

			}


			//calculate frequency RoC

			if(i==0){
				dfreq[0] = (freq[0]-freq[99]) * 2.0 * freq[0] * freq[99] / (freq[0]+freq[99]);
			}
			else{
				dfreq[i] = (freq[i]-freq[i-1]) * 2.0 * freq[i]* freq[i-1] / (freq[i]+freq[i-1]);
			}

			if (dfreq[i] > 100.0){
				dfreq[i] = 100.0;
			}


			i =	++i%100; //point to the next data (oldest) to be overwritten

		}

		//clear old graph to draw new graph
		alt_up_pixel_buffer_dma_draw_box(pixel_buf, 101, 0, 639, 199, 0, 0);
		alt_up_pixel_buffer_dma_draw_box(pixel_buf, 101, 201, 639, 299, 0, 0);

		for(j=0;j<99;++j){ //i here points to the oldest data, j loops through all the data to be drawn on VGA
			if (((int)(freq[(i+j)%100]) > MIN_FREQ) && ((int)(freq[(i+j+1)%100]) > MIN_FREQ)){
				//Calculate coordinates of the two data points to draw a line in between
				//Frequency plot
				line_freq.x1 = FREQPLT_ORI_X + FREQPLT_GRID_SIZE_X * j;
				line_freq.y1 = (int)(FREQPLT_ORI_Y - FREQPLT_FREQ_RES * (freq[(i+j)%100] - MIN_FREQ));

				line_freq.x2 = FREQPLT_ORI_X + FREQPLT_GRID_SIZE_X * (j + 1);
				line_freq.y2 = (int)(FREQPLT_ORI_Y - FREQPLT_FREQ_RES * (freq[(i+j+1)%100] - MIN_FREQ));

				//Frequency RoC plot
				line_roc.x1 = ROCPLT_ORI_X + ROCPLT_GRID_SIZE_X * j;
				line_roc.y1 = (int)(ROCPLT_ORI_Y - ROCPLT_ROC_RES * dfreq[(i+j)%100]);

				line_roc.x2 = ROCPLT_ORI_X + ROCPLT_GRID_SIZE_X * (j + 1);
				line_roc.y2 = (int)(ROCPLT_ORI_Y - ROCPLT_ROC_RES * dfreq[(i+j+1)%100]);

				//Draw
				alt_up_pixel_buffer_dma_draw_line(pixel_buf, line_freq.x1, line_freq.y1, line_freq.x2, line_freq.y2, 0x3ff << 0, 0);
				alt_up_pixel_buffer_dma_draw_line(pixel_buf, line_roc.x1, line_roc.y1, line_roc.x2, line_roc.y2, 0x3ff << 0, 0);
			}
		}
		vTaskDelay(10);

	}
}

// The following test prints out status information every 3 seconds.
void print_status_task(void *pvParameters)
{
	while (1)
	{
		vTaskDelay(3000);
		printf("****************************************************************\n");
		printf("Hello From FreeRTOS Running on NIOS II.  Here is the status:\n");
		printf("\n");
		printf("The current frequency is:         %lf\n", temp);
		printf("\n");
		printf("The frequency threshold is: %lf\n",freqThresh );
		printf("\n");
		printf("The current ROC is : %lf\n", ROCThresh);
		printf("\n");
		printf("The ROC threshold is: %lf\n", ROCThresh);
		printf("\n");
		printf("****************************************************************\n");
		printf("\n");
	}
}

void keyboard_control_task(void *pvParameters)
{	int notiValue;
	int i;
	int j = 0;
	while (1)
	{	notiValue = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		//printf("Scan code: %x\n", byte);
	// press 'R' in the keyboard will enter change roc mode, click numbers on the keypad
	//and click 'ENTER' will save the change

	if((byte == 45)&&(keyboardMode ==0)){
		memset(testStr, 0, 30);
		j =0;
		printf("you are about to change ROC threshold\n");
		keyboardMode =2;
	}
	if(keyboardMode ==2){
		if(byte == 90){
		for(i = 0; i <20; i++){
			if(i%2 == 0){
				oddStr[j++] = testStr[i];
			}
		}
		ROCThresh = strtod(oddStr, &oddStr);
		printf("double ROC Threshold: %lf\n", ROCThresh);
		keyboardMode =0;
		}
		else{
					for(i = 0; i < 11; i ++){
						char keyPadNum[4];
						if(byte == numbers[i]){
							if(i != 10){
							sprintf(keyPadNum, "%d", i);
							//printf("new freq Threshold: %s\n", keyPadNum);
							strncat(testStr, &keyPadNum[0], 1);
							//printf("new ROC Threshold: %s\n", testStr);

						}else{
							strncat(testStr, &".", 1);
							//printf("new ROC Threshold: %s\n", testStr);
						}
						}
					}
				}

	}	// press 'F' will enter change frequency mode , click numbers on the keypad
		//and click 'ENTER' will save the change
		if((byte == 43)&&(keyboardMode ==0)){
			memset(testStr, 0, 30);
			j =0;
			printf("you are about to change frequency threshold\n");
			keyboardMode =1;
		}
		if(keyboardMode ==1){
			if(byte == 90){
			for(i = 0; i <20; i++){
				if(i%2 == 0){
					oddStr[j++] = testStr[i];
				}
			}
			freqThresh = strtod(oddStr, &oddStr);
			printf("double freq Threshold: %lf\n", freqThresh);
			keyboardMode =0;
			}else{
			for(i = 0; i < 11; i ++){
				char keyPadNum[4];
				if(byte == numbers[i]){
					if(i != 10){
					sprintf(keyPadNum, "%d", i);
					//printf("new freq Threshold: %s\n", keyPadNum);
					strncat(testStr, &keyPadNum[0], 1);
					//printf("new freq Threshold: %s\n", testStr);

				}else{
					strncat(testStr, &".", 1);
					//printf("new freq Threshold: %s\n", testStr);
				}
				}
			}
		}
		}

		previousbyte = byte;
	}
}

void stabilityMonitorTask(void *p){
	int notiValue;
	int counter = 0;
	while (1)
		{	notiValue = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

			currentROC = (temp-previousFreq) * 2.0 *temp* previousFreq / (temp+previousFreq);
			currentROC = fabs(currentROC);
			//printf("real time frequency: %lf\n", temp);
			//printf("real time ROC: %lf\n", currentROC);
			previousFreq = temp;

			// record 5 measurements
			if (counter < 5) {
				recent_five_sheds[counter] = temp;
				counter++;
			} else {
				counter = 0;
			}

			// print the 5 measurements
//			int loop;
//			for(loop = 0; loop < 5; loop++){
//				printf("%d ", recent_five_sheds[loop]);
//			}

			push_button = IORD_ALTERA_AVALON_PIO_DATA(PUSH_BUTTON_BASE);
//			IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, push_button);
			//printf("push_button_value : %d\n",push_button);
			if((push_button == 3)&&(mantainMode ==false)){

				mantainMode = true;
				IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, 0x00);	// turn off all green LEDs

			}if((push_button == 7)&&(mantainMode ==true)){
				mantainMode = false;
			}



			if((loadManage ==false )&&(mantainMode == false)&&((temp <= freqThresh )||(currentROC >= ROCThresh))){
				loadManage = true;
				unstableRemain = false;
				numOfShed = numOfShed+1;
				timerCount = 0;
				xTimerReset(timer, 10 );
				printf("unstable detected, %d load sheded \n",numOfShed);
			}
			if((loadManage ==true )&&((temp >= freqThresh )&&(currentROC <= ROCThresh))){
				loadManage = false;
				unstableRemain = false;
				numOfShed = 0;
				timerCount = 0;
				printf("back to normal\n");
			}

			if((loadManage ==true )&&(mantainMode == true)){
				loadManage = false;
				unstableRemain = false;
				numOfShed = 0;
				timerCount = 0;
				printf("back to normal\n");

			}

			if ((loadManage == true) &&(unstableRemain == true)&&(mantainMode == false)){
				numOfShed = numOfShed+1;
				unstableRemain = false;
				xTimerReset(timer, 10 );
				printf("unstable remained, %d load sheded \n",numOfShed);

			}
		}
}


void loadCtlTask(void *p){

	int load_output;
	int fill_flag = 0;
	int counter = 0;
	int out = 0;

	while(1){
		if(loadManage ==true){
			int* output = shedLoads(numOfShed,sw_result_bin);
			out = output;
			load_output = binaryToDecimal(out);
		}else{
			load_output = binaryToDecimal(sw_result_bin);
			//printf(" %d ",  load_output);
		}

//		update_stats();


		//IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, buttonValue);
	    IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, load_output);    //light red LEDs according to switch positions
	    IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, check_output(load_output));
	    vTaskDelay(50);
	    printf("\n%d\n", out);
	}

}


//int update_stats():



int check_output(int input){
	int out = 0x00;
	switch (input){
		case 262143:
			out = 0x1;
			out = 0x00;
			break;
		case 262142:
			out = 0x1;
			break;
		case 262140:
			out = 0x3;
			break;
		case 262136:
			out = 0x7;
			break;
		case 262128:
			out = 0xF;
			break;
		case 262112:
			out = 0x1F;
			break;
		case 262080:
			out = 0x3F;
			break;
		case 262016:	// 0b111111111110000000
			out = 0x7F;
//			out = 0xFF;
			break;
		default:
			out = 0x00;
			break;
	}
	return out;
}




int* shedLoads(int numOfShed,int sw_result_bin[]){

	int len = current_sw_size;
	int loadAfterShed[len];
	int m = 0;
	int count = 0;

	for (m = 0; m < len; m++) {
		loadAfterShed[m] = sw_result_bin[m];
	}


	for (m = 0; m< len ; m++) {
	        if((loadAfterShed[m] == 1)&&(count < numOfShed)){
	        	loadAfterShed[m] = 0;
	        	count ++;
	        }
	}


	return loadAfterShed;
}



int binaryToDecimal(int sw_result_bin[]){
	int m = 0;
	int dec_value = 0;
	int base = 1;
	int len = current_sw_size;
	int output[len];
	//printf(" %d ",  len);
	  for (m = 0; m < len; m++) {
		  output[m] = sw_result_bin[m];
	    }

	for (m = 0; m < len; m++) {
	        if(output[m] == 1){
	            dec_value += base;
	        }
	        base = base * 2;
}
	return dec_value;
}


void switchPollingTask(void *p){
	int k,l;
	while(1){
	if(loadManage == false){
	sw_result=IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE); //read slide switches
	for(k=0; sw_result>0; k++){
		sw_result_bin[k] = sw_result%2;
		sw_result = sw_result /2;
	}
	current_sw_size =k;
	//printf("real time ROC:");
    for(l = k - 1; l >= 0; l--)  {
        //printf(" %d ", sw_result_bin[l]);
    }
	}
    vTaskDelay(150);
	}
}


void freq_relay(){
	#define SAMPLING_FREQ 16000.0
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	temp = SAMPLING_FREQ/(double)IORD(FREQUENCY_ANALYSER_BASE, 0);


	xQueueSendToBackFromISR( Q_freq_data, &temp, pdFALSE );
	xTaskNotifyFromISR(stableTaskHandle, 1, eSetValueWithOverwrite, &xHigherPriorityTaskWoken);
	return;
}

void ps2_isr(void* ps2_device, alt_u32 id){

	alt_up_ps2_read_data_byte_timeout(ps2_device, &byte);
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	  // Send a notification to index 0 with value 1
	xTaskNotifyFromISR(keyboardTaskHandle, 1, eSetValueWithOverwrite, &xHigherPriorityTaskWoken);
}

void vTimerCallback(xTimerHandle t_timer){
	if((loadManage == true) &&(unstableRemain == false)){
		//printf("it reset\n");
		unstableRemain = true;
	}
	//timerCount++;
	//printf("current time is : %d\n",timerCount*5);
	//printf("unstable remained\n");
}


void push_button_irq(){
	IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, IORD_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE));
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7); //write 1 to clear all detected falling edges
	return;
}

int main()
{
	Q_freq_data = xQueueCreate(100,sizeof(double));
	alt_up_ps2_dev * ps2_device = alt_up_ps2_open_dev(PS2_NAME);

	alt_irq_register(FREQUENCY_ANALYSER_IRQ, 0, freq_relay);

	alt_up_ps2_enable_read_interrupt(ps2_device);
	alt_irq_register(PS2_IRQ, ps2_device, ps2_isr);

	timer = xTimerCreate("Timer Name", 500, pdTRUE, NULL, vTimerCallback);
	if (xTimerStart(timer, 0) != pdPASS){
			printf("Cannot start timer");

	}

	IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PUSH_BUTTON_BASE, 0x7); //enable interrupt for all three push buttons (Keys 1-3 -> bits 0-2)
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7); //write 1 to edge capture to clear pending interrupts
	alt_irq_register(PUSH_BUTTON_IRQ, 0, push_button_irq);  //register ISR for push button interrupt request

	xTaskCreate( PRVGADraw_Task, "DrawTsk", configMINIMAL_STACK_SIZE, NULL, PRVGADraw_Task_P, &PRVGADraw );
	//xTaskCreate(print_status_task, "print_status_task", configMINIMAL_STACK_SIZE, NULL, PRINT_STATUS_TASK_PRIORITY, NULL);
	xTaskCreate(keyboard_control_task, "keyboardTsk", configMINIMAL_STACK_SIZE, NULL, KEYBOARD_TASK_PRIORITY, &keyboardTaskHandle);
	xTaskCreate(stabilityMonitorTask, "stableMonitorTsk", configMINIMAL_STACK_SIZE, NULL, STABLE_MONITOR_TASK_P, &stableTaskHandle);
	xTaskCreate(loadCtlTask, "loadCtlTask", configMINIMAL_STACK_SIZE, NULL, LOAD_CONTROL_TASK_P, &loadCtlTaskHandle);
	xTaskCreate(switchPollingTask, "switchPollingTask", configMINIMAL_STACK_SIZE, NULL, LOAD_CONTROL_TASK_P, &switchPollingTaskHandle);


	vTaskStartScheduler();

	while(1)

  return 0;
}

