#include "FreeRTOS.h"
#include "task.h"

#include <math.h>
#include "stm32f10x.h"
#include "bit.h"
#include "mpu6050.h"
#include "motor.h"
#include "uart.h"
#include "wifi.h"
#include "pid.h"
#include "tty.h"

#define Kp      100.0f      //比例增益支配率(常量)
#define Ki      0.002f      //积分增益支配率
#define halfT   0.001f      //采样周期的一半

float g_q0 = 1, g_q1 = 0, g_q2 = 0, g_q3 = 0;   //Quaternion
float g_exInt = 0, g_eyInt = 0, g_ezInt = 0;
float g_Yaw, g_Pitch, g_Roll;



//ms
void delay(volatile unsigned int count) {
    for(count *= 12000; count!=0; count--);
}

void Comput(SixAxis cache) {

    float norm;     //模
    float vx, vy, vz;
    float ex, ey, ez;

    norm = sqrt(cache.aX*cache.aX + cache.aY*cache.aY + cache.aZ*cache.aZ);     //取模

    //向量化
    cache.aX = cache.aX / norm;
    cache.aY = cache.aY / norm;
    cache.aZ = cache.aZ / norm;

    //估计方向的重力
    vx = 2 * (g_q1 * g_q3 - g_q0 * g_q2);
    vy = 2 * (g_q0 * g_q1 + g_q2 * g_q3);
    vz = g_q0*g_q0 - g_q1*g_q1 - g_q2*g_q2 + g_q3*g_q3;

    //错误的领域和方向传感器测量参考方向几件的交叉乘积的总和
    ex = (cache.aY * vz - cache.aZ * vy);
    ey = (cache.aZ * vx - cache.aX * vz);
    ez = (cache.aX * vy - cache.aY * vx);

    //积分误差比例积分增益
    g_exInt += ex * Ki;
    g_eyInt += ey * Ki;
    g_ezInt += ez * Ki;

    //调整后的陀螺仪测量
    cache.gX += Kp * ex + g_exInt;
    cache.gY += Kp * ey + g_eyInt;
    cache.gZ += Kp * ez + g_ezInt;

    //整合四元数率和正常化
    g_q0 += (-g_q1 * cache.gX - g_q2 * cache.gY - g_q3 * cache.gZ) * halfT;
    g_q1 += (g_q0 * cache.gX + g_q2 * cache.gZ - g_q3 * cache.gY) * halfT;
    g_q2 += (g_q0 * cache.gY - g_q1 * cache.gZ + g_q3 * cache.gX) * halfT;
    g_q3 += (g_q0 * cache.gZ + g_q1 * cache.gY - g_q2 * cache.gX) * halfT;

    //正常化四元
    norm = sqrt(g_q0*g_q0 + g_q1*g_q1 + g_q2*g_q2 + g_q3*g_q3);
    g_q0 = g_q0 / norm;
    g_q1 = g_q1 / norm;
    g_q2 = g_q2 / norm;
    g_q3 = g_q3 / norm;

    g_Pitch = asin(-2 * g_q1 * g_q3 + 2 * g_q0 * g_q2) * 57.3;
    g_Roll = atan2(2 * g_q2 * g_q3 + 2 * g_q0 * g_q1, -2 * g_q1*g_q1 - 2 * g_q2*g_q2 + 1) * 57.3;
    g_Yaw = atan2(2 * (g_q1 * g_q2 + g_q0 * g_q3), g_q0*g_q0 + g_q1*g_q1 - g_q2*g_q2 - g_q3*g_q3) * 57.3;
}

#define DEBUG_BLDC		//Config

#if defined (DEBUG_MPU6050_EULER) || defined (DEBUG_MPU6050_SOURCEDATA) || defined (DEBUG_BLDC)
    SixAxis sourceData;
#endif

// float InnerLast;			//保存内环旧值以便后向差分
// float OutterLast;		//保存外环旧值以便后向差分
// float *Feedback;			//反馈数据, 实时的角度数据
// float *Gyro;				//角速度
// float Error;				//误差值
// float p;					//比例项(内环外环共用)
// float i;					//积分项(仅用于外环)
// float d;					//微分项(内环外环共用)
// short output;			//PID输出, 用来修改PWM值, 2字节
// __IO uint16_t *Channel1;	//PWM输出, 通道1
// __IO uint16_t *Channel2;	//PWM输出, 通道2
#ifdef DEBUG_BLDC
    pid_st g_pid_roll = {
        .InnerLast  = 0,
        .OutterLast = 0,
        .Feedback   = &g_Roll,
        .i          = 0,
        .Channel1   = &MOTOR2,
        .Channel2   = &MOTOR4,
        .Gyro       = &sourceData.gX,
    };
#endif

void mpu_task() {
	while(1) {
		MPU6050_getStructData(&sourceData);
        Comput(sourceData);
		vTaskDelay(10);
	}
}

#ifdef DEBUG_BLDC
	void pid_task() {
		while(1) {
			pid_SingleAxis(&g_pid_roll, 0);
			vTaskDelay(10);
		}
	}
#endif

void uart_task() {
	while(1) {
		uart_sendStr("Pitch Angle: ");
        uart_Float2Char(g_Pitch);

        uart_sendStr("; Roll Angle: ");
        uart_Float2Char(g_Roll);

        uart_sendStr("; Yaw Angle: ");
        uart_Float2Char(g_Yaw);

        UART_CR();

		vTaskDelay(100);
	}
}

void uart_debugPID() {
	while(1) {
		TTY_CLEAR();

		TTY_RED();
		uart_sendStr(" Motor占空比: ");
		TTY_NONE();
		TTY_BLUE();
		uart_showData(*g_pid_roll.Channel1);
		uart_sendStr("\t");
		uart_showData(*g_pid_roll.Channel2);
		TTY_NONE();

		uart_sendStr("\n\rRoll:\t");
		uart_Float2Char(*g_pid_roll.Feedback);

		uart_sendStr("\tGyro:\t");
		uart_Float2Char(*g_pid_roll.Gyro);

		uart_sendStr("\n\rP:\t");
		uart_Float2Char(g_pid_roll.p);

		uart_sendStr("\n\rI:\t");
		uart_Float2Char(g_pid_roll.i);

		uart_sendStr("\n\rD:\t");
		uart_Float2Char(g_pid_roll.d);

		uart_sendStr("\n\r=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=\n\rInner Cache:\t");
		uart_Float2Char(g_pid_roll.InnerLast);
		uart_sendStr("\n\rOutter Cache:\t");
		uart_Float2Char(g_pid_roll.OutterLast);

		uart_sendStr("\n\rOutput:\t\t");
		TTY_RED();
		uart_showData(g_pid_roll.output);
		TTY_NONE();
		uart_sendStr("\n\r");
		vTaskDelay(100);
	}
}

int main() {
	#ifdef DEBUG_BLDC
		//Brushless motor auto init
	    MOTOR_SETTING();
	#endif

    uart_init(72, 115200);
    uart_sendStr("Config MPU6050...");
    UART_CR();
    MPU_init();
    uart_sendStr("MPU6050 Connect Success!");
    UART_CR();

	xTaskCreate(uart_task, "UART_TASK", 100, NULL, 1, NULL);
	xTaskCreate(mpu_task, "MPU_TASK", 100, NULL, 3, NULL);
	xTaskCreate(pid_task, "PID_TASK", 100, NULL, 2, NULL);
	vTaskStartScheduler();
	uart_sendStr("Stack Overflow...");
	while(1);

//	Main Loop
    while(1) {
		#ifdef DEBUG_WIFI
			wifi_Config();
			while(1) {
				wifi_sendCmd("AT+CIPSEND=0,20");
				delay(50);
				wifi_sendCmd("<html>aki<br></html>");
				delay(1000);
			}
		#endif
		#ifdef DEBUG_MPU6050_SOURCEDATA
			MPU6050_getStructData(&sourceData);
			MPU6050_debug(&sourceData);
		#endif

		#if defined (DEBUG_BLDC)
			MPU6050_getStructData(&sourceData);
			Comput(sourceData);

			pid_SingleAxis(&g_pid_roll, 0);
			TTY_CLEAR();

			TTY_RED();
			uart_sendStr(" Motor占空比: ");
			TTY_NONE();
			TTY_BLUE();
			uart_showData(*g_pid_roll.Channel1);
			uart_sendStr("\t");
			uart_showData(*g_pid_roll.Channel2);
			TTY_NONE();

			uart_sendStr("\n\rRoll:\t");
			uart_Float2Char(*g_pid_roll.Feedback);

			uart_sendStr("\tGyro:\t");
			uart_Float2Char(*g_pid_roll.Gyro);

			uart_sendStr("\n\rP:\t");
			uart_Float2Char(g_pid_roll.p);

			uart_sendStr("\n\rI:\t");
			uart_Float2Char(g_pid_roll.i);

			uart_sendStr("\n\rD:\t");
			uart_Float2Char(g_pid_roll.d);

			uart_sendStr("\n\r=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=_=\n\rInner Cache:\t");
			uart_Float2Char(g_pid_roll.InnerLast);
			uart_sendStr("\n\rOutter Cache:\t");
			uart_Float2Char(g_pid_roll.OutterLast);

			uart_sendStr("\n\rOutput:\t\t");
			TTY_RED();
			uart_showData(g_pid_roll.output);
			TTY_NONE();
			uart_sendStr("\n\r");

		#endif

		#ifdef DEBUG_MPU6050_EULER
			MPU6050_getStructData(&sourceData);

			Comput(sourceData);


			uart_sendStr("Pitch Angle: ");
			uart_Float2Char(g_Pitch);


			uart_sendStr("; Roll Angle: ");
			uart_Float2Char(g_Roll);

			uart_sendStr("; Yaw Angle: ");
			uart_Float2Char(g_Yaw);

			UART_CR();

			delay(100);
		#endif
    }
}
