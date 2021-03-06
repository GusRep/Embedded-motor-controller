/* Archivo unico.                       AUTOR: GUSTAVO D. GIL
                                     FECHA: 22/05/2011

Control de velocidad y posicion por IRQ en FixedPoint.

// MENU QUE SE DESPLIEGA POR DEFAULT        
            printf("\n\Menu:");   
            printf("\n\r   '1' POS -> POTE");     
            printf("\n\r   '2' POS -> TECLADO");
            printf("\n\r   '3' VEL -> POTE");
            printf("\n\r   '4' VEL -> TECLADO\n\r");
            printf("\n\r   '5' Edicion Controlador POS");
            printf("\n\r   '6' Edicion Controlador VEL");
            printf("\n\r   '7' Controlador por defecto Ahhh ... g04");
//
// Recibe:  nada
// Retorna: nada
// =====
*/

// Librerias
#include "main.h"
#include <stdio.h>
#include <stdlib.h>

// Redefinicion de tipo de dato (32 bits signado)
typedef signed long long int g;

//Macros
#define FP(x,q)         ((x)*(1<<(q)))          // FixedPoint (Res: 2^q)
#define DD(x,q) (((x)>=0) ? ((x)>>(q)) : ~((~(x))>>(q)))
//#define PROD_FP(a,b,q)  (((a)*(b))>>(q))
#define PROD_FP(a,b,q2) ((((a)*(b))>=0) ? (((a)*(b))>>(q2)) : ~((~((a)*(b)))>>(q2)))
#define DIV_FP(a,b,q3)   (((a)<<(q3))/(b))

// Constantes
#define NVEL    1         // Grado del polinomio y[n] Vel
#define MVEL    1         // Grado del polinomio u[m] Vel
#define NPOS    0         // Grado del polinomio y[n] Pos
#define MPOS    1         // Grado del polinomio u[m] Pos
#define Q        8         // Resolucion de FixedPoint = 2^Q
#define RES     100
#define ESPERA  20
#define ESPERA2 500000

// Codificacion de 7 segmentos
#define N0 0xC0
#define N1 0xF9
#define N2 0xA4
#define N3 0xB0
#define N4 0x99
#define N5 0x92
#define N6 0x82
#define N7 0xF8
#define N8 0x80
#define N9 0x90
#define DIG_OFF 0x00      // Apagado del digito LCD


// COEFICIENTES DE LOS CONTROLADORES:
// ==================================
// Coeficientes para el control de Velocidad
#define B0_vel   FP(0.36,Q)               //  0.17  0.36  0.45
#define B1_vel   FP(-0.33,Q)               // -0.07 -0.33 -0.32
// Coeficientes para el control de Posicion
#define B0_pos   FP(0.28,Q)               //  0.20  0.28  0.75 


///////////////////////////// Registros del PIC //////////////////////////
// Address de los puertos
#byte PORTA=0x05     //6 pines
#byte PORTB=0x06     // y dir 0x106
#byte PORTC=0x07
#byte PORTD=0x08
#byte PORTE=0x09     //3 pines

// Registros de configuracion I/O (TRIS)
#byte TRISA=0x85
#byte TRISB=0x86     // y dir 0x186
#byte TRISC=0x87
#byte TRISD=0x88
#byte TRISE=0x89

//definicion ADCON
#byte ADCON0 = 0x01F
#byte ADCON1 = 0x09F

//variables
#bit pulsador = PORTB.0
#bit uni      = PORTE.0
#bit dec      = PORTE.1
#bit cen      = PORTE.2


// Variables globales
int noise=0;
int modo;          //tipo de control
int texto[10], text1[5], text2[10], text3[5];

g enuevo, valor, number, b0, b1;   

g bvel[MVEL+1], avel[NVEL+1], velrefrpm, velact, velrpm, refevel;
g errovel[MVEL+1], ukvel[NVEL+1];

g bpos[MPOS+1], apos[NPOS+1], posrefgr, posact, posgr, refepos;
g erropos[MPOS+1], ukpos[NPOS+1];

g numero=0;   //numero a mostrar

int param=0;   //parametros por defecto de los controladores
int u, d, c, display=0;       //seleccion del display y barrido


// ProdEsc_FP: realiza el producto escalar entre dos vectores.
// ===========
// Hace uso de la macro:  #define PROD_FP(a,b,q)  (((a)*(b))>>(q))                     
// Realmente usa: PROD_FP(a,b,q2) ((((a)*(b))>=0) ? (((a)*(b))>>(q2)) : ~((~((a)*(b)))>>(q2)))
//
// Recibe: dos vectores tipo g y la longitud de los mismos
// Retorna: un resultado escalar tipo g
g ProdEsc_FP(g vec1[], g vec2[], int l)
{
   g acc=0;
   g acc2;
   int i;
   for(i=0;i<l;i++)
   {
      acc2=PROD_FP(vec1[i],vec2[i],Q);
      acc=acc+acc2;
   }
   return acc;
}


// lcd2: Convierte un numero decimal de 0 a 9 en codigo 7 segmentos 
// =====
// El display es del tipo 7 segmentos
// Posee DP:punto decimal, APAGADO del digito y ERROR
//
// Recibe: un numero en int
// Retorna: un codigo 7 seg en int
/*-----------------------------------------------------------------
Nota: el apagado del los digitos y el punto decimal no estan implementados !
------------------------------------------------------------------*/
int lcd2(int n)
{
   int code[10];
   *(code+0)=N0;
   *(code+1)=N1;
   *(code+2)=N2;
   *(code+3)=N3;
   *(code+4)=N4;
   *(code+5)=N5;
   *(code+6)=N6;
   *(code+7)=N7;
   *(code+8)=N8;
   *(code+9)=N9;

   return code[n];
}

// display_FP: Funcion para descomponer numero de 3 digitos en los displays
// ===========
//
// Recibe: un numero en g
// Retorna: no retorna dada, afecta a var. globales u, d, c
void display_FP(g number)
{  
   g naux;
   int aux;   
   if (number<0) 
      naux=-number;
   else
      naux=number;
   u = lcd2(naux%10);   //unidades      
   naux = naux/10;
   d = lcd2(naux%10);   //decenas
   naux = naux/10;
   aux = lcd2(naux%10);
   if (number<0 && number>-200)
   {  if(aux==N0)
         c = 0xBF;   //signo "-"
      if(aux==N1)
         c = 0xB9;   //numero "-1"
   }
   else 
      c=aux;
}


// AdquiereValor: convierte un string en un numero tipo g
// ==============
//
// Recibe: un puntero a un vector de caracteres y un puntero a un string
// Retorna: 
g AdquiereValor(char* cmd, char* palabra)
{
   char *dp;
   g valor=0;
      dp=strstr(cmd,palabra);   // busca una cadena en otra
      if (dp!=NULL)
      {   dp=dp+strlen(palabra);
         valor = atoi32(dp);      
      }
   return valor;
}


// ini_cola: inicializa arrays tipo g
// ============
//
// Recibe: puntero al vector de Stack, largo del Stack  <Globales> 
// Retorna: nada
void ini_cola(g *tabla, int l)
{
   int i;
   for (i=0;i<l;i++)
   {
      tabla[i]=0;
   }
}


// cola_FP: cola FIFO.
// ========
//
// Recibe: puntero al vector de Stack previamente definido, largo del Stack, dato a ingresar
// Retorna: nada
void cola_FP(g *tabla, int l, g dataNew)      
{
   int i;
   // SHIFT los datos de la cola FIFO (poco eficiente con muchos datos)
   for (i=l-1;i>0;i--)
   {
      tabla[i]=tabla[i-1];
   }
   // Insertamos el dataNew
   tabla[0]=dataNew;
}


// lineal_FP: Convierte un dato de entrada, en otro de salida a traves de una funcion lineal
// ==========
// Hace uso de las macros:  
//   #define PROD_FP(a,b,q2) ((((a)*(b))>=0) ? (((a)*(b))>>(q2)) : ~((~((a)*(b)))>>(q2)))
//   #define DIV_FP(a,b,q3)   (((a)<<(q3))/(b))
//   #define DD(x,q) (((x)>=0) ? ((x)>>(q)) : ~((~(x))>>(q)))
//
// Recibe: dato de entrada_X , Y_max, Y_min, X_max, X_min  <<g>>
// Retorna: dato convertido_Y (g)
g lineal_FP (g Xin, g Xmin, g Xmax, g Ymin, g Ymax)  
{           
   g Yout;
   // Ecuacion de la recta:
   // Yout=Ymin+(Xin-Xmin)*((Ymax-Ymin)/(Xmax-Xmin));
   Yout=Ymin+(PROD_FP((Xin-Xmin),DIV_FP((Ymax-Ymin),(Xmax-Xmin),Q),Q));
   Yout=DD(Yout,Q);
   return Yout;
}


// GdeZ: Funcion transferencia en el dominio Z  <<< G[z] >>>.
// =====
// Recibe: un vector de coeficientes         b[m]      Long: M+1
//         un vector de coeficientes         a[n]      Long: N+1
//         un vector de entradas temporales  u         
//         un vector de salidas anteriores   y         
//         grado del polinomio (valor de M)
//         grado del polinomio (valor de N)
// Retorna: salida del sistema
//------------------------------------------------------------------------------------
// En n (tiempo discreto):
//
//  y[k] = b[0]*u[k] + b[1]*u[k-1] + ... + b[m]*u[k-m]-a[1]*y[k-1] - ... - a[n]*y[k-n]
//
//  Siendo: u[m] las entradas temporales
//          y[n] las salidas temporales
//
//
// En Z (plano Z):
//
//          Y[z]    b0 + b1.z^-1 + b2.z^-2 + ... + bm.z^-m
//  G[z] = ----- = ----------------------------------------
//          U[z]    1 + a1.z^-1 + a2.z^-2 + ... + an.z^-n
//
//  Siendo: U[z] las entradas transformadas
//          Y[z] las salidas transformadas
//
g GdeZ(g uk[],g bk[],int m, g yk[],g ak[],int n)
{ 
   g primero,segundo,out;
   
   primero=ProdEsc_FP(uk,bk,m);
   segundo=ProdEsc_FP(yk,ak,n);  // los coeficientes de segundo, se restan
   out=primero-segundo;
   return out; 
}


// FUNCIONES DE CONTROL //
// ======================
//Velocidad
void velocidad(void)
{   g volt;
    set_adc_channel(0);
    velact=Read_ADC();
    delay_us(ESPERA);
    //interpolacion para obtener la vel actual en rpm
    velrpm=lineal_FP(FP(velact,Q),(g)FP(200,Q),(g)FP(823,Q),(g)FP(-62,Q),(g)FP(62,Q));
     
   if(modo==3)
      {  set_adc_channel(1);
         delay_us(ESPERA); 
         refevel=READ_ADC();
         delay_us(ESPERA);
         velrefrpm=lineal_FP(FP(refevel,Q),(g)FP(31,Q),(g)FP(997,Q),(g)FP(-90,Q),(g)FP(90,Q));
       ///printf("\n\r velrefrpm    %ld \r",velrefrpm);
      }

   enuevo=velrefrpm-velrpm;   //calculo del error
   ///  printf("\n\r velrefrpm   -velrpm    %ld      %ld    %ld \r",velrefrpm,velrpm, velact);
   enuevo=FP(enuevo,Q);
    cola_FP(errovel,MVEL+1,enuevo);
    cola_FP(ukvel,NVEL+1,0);
   
    ukvel[0]= GdeZ(errovel,bvel,MVEL+1, ukvel, avel, NVEL+1);
    volt = lineal_FP(ukvel[0],FP(18,Q),FP(-18,Q),FP(20,Q),FP(1003,Q));
   
   ///printf("\n\r UKVEL(0)   VOLT    %ld      %ld    \r",ukvel[0],volt);

    if (volt>=1003)      //limitaciones del PWM
         volt=1003;
    if (volt<=20)
         volt=20;

    set_pwm1_duty(volt); 
}


//Posicion
void posicion(void)
{  g volt;
   set_adc_channel(2);
   posact=Read_ADC();
   delay_us(ESPERA);
   if (modo==2)  //POS TECLADO
      posgr=lineal_FP(FP(posact,Q),(g)FP(40,Q),(g)FP(1020,Q),(g)FP(0,Q),(g)FP(360,Q));
   if (modo==4)   //POS POTE
      posgr=lineal_FP(FP(posact,Q),(g)FP(30,Q),(g)FP(993,Q),(g)FP(150,Q),(g)FP(-150,Q)); 
   enuevo=posrefgr-posgr;              
   enuevo=FP(enuevo,Q);
   cola_FP(erropos,MPOS+1,enuevo);
   cola_FP(ukpos,NPOS+1,0);

   ukpos[0]= GdeZ(erropos,bpos,MPOS+1, ukpos, apos, NPOS+1);
   if (modo==2)
      volt = lineal_FP(ukpos[0],FP(18,Q),FP(-18,Q),FP(0,Q),FP(1023,Q));
   if (modo==4)
      volt = lineal_FP(ukpos[0],FP(16,Q),FP(-20,Q),FP(1023,Q),FP(0,Q));

   if (volt>=1023)
      volt=1023;
   if (volt<=20)
      volt=20;
      
   set_pwm1_duty(volt); 
}

//Control de Velocidad con ingreso de referencia por teclado
void ContVel(void)   
{   
   if((param==0)|(param==1))       //mantenemos el controlador disenado
   {  bvel[0]=B0_vel;
      bvel[1]=B1_vel;
   }
   if(param==1)               //en caso se desee cambiar los datos del controlador
   {  bvel[0]=FP(b0,Q);   
      bvel[0]=bvel[0]/RES;
      bvel[1]=FP(b1,Q);
      bvel[1]=bvel[1]/RES;
   }

   avel[0]=FP(1,Q);
   avel[1]=FP(-1,Q);
   velocidad();
}


//Control de Posicion con ingreso de referencia por teclado
void ContPos(void)
{   
   if((param==0)|(param==2))       //en caso se mantenga el controlador dise�ado
      bpos[0]=B0_pos;

   if(param==1)               //cambio  del controlador
      {  bpos[0]=FP(b0,Q);
         bpos[0]=bpos[0]/RES;
      }
   bpos[1]=FP(0,Q);
   apos[0]=FP(0,Q);
   posicion();
}


//Referencia de Velocidad
Void RefVel()
{
   set_adc_channel(1);
   delay_us(ESPERA); 
   refevel=READ_ADC();
   delay_us(ESPERA);
   velrefrpm=lineal_FP(FP(refevel,Q),(g)FP(31,Q),(g)FP(997,Q),(g)FP(-60,Q),(g)FP(60,Q)); 
   numero=velrefrpm;
   velocidad();
}


//Referencia de Posicion
void RefPos(void)
{
   set_adc_channel(1);
   delay_us(ESPERA); 
   refepos=Read_ADC();
   delay_us(ESPERA);
   ///printf("\n\r RefPOS    %ld       \r",refepos);
   posrefgr=lineal_FP(FP(refepos,Q),(g)FP(31,Q),(g)FP(997,Q),(g)FP(150,Q),(g)FP(-150,Q)); 
   numero=-posrefgr;
   posicion();
}


// IRQ Timer0: refresco de los displays de 7 segmentos (un digito por IRQ)
// ===========      // Adicional "rutina de silencio"
#int_TIMER0
void TIMER0_isr(void)
{   switch(display)
   {   
     case 0:
         PORTD=u;            // Unidades   
         cen=1;             // apaga el display de centenas
         uni=0;             // enciende el display de unidades
         display++;         // incrementa la cuenta para que la pr�xima 
         break;             // IRQ entre en case siguiente
      case 1:
         PORTD=d;            // Decenas   
         uni=1;
         dec=0;
         display++;   
         break;
      case 2:
         PORTD=c;            // Centenas   
         dec=1;
         cen=0;
         display=0;         // resetea la cuenta   
         break;
   }
   if (!pulsador)         // rutina de silencio (Aleluyaaa)
   {
    noise=~noise;
    if (noise)
      {   
         TRISC= 0x84;      // apaga la salida PWM por hardware
         delay_us(ESPERA2);
      }
      else
      {  TRISC= 0x80;      // activa la salida PWM por hardware
         delay_us(ESPERA2);
      }
   }
}


// IRQ RDA: Dato recibido en USART
// ========
#int_RDA
void RDA_isr(void)
{
   int caracter;
   output_bit(PIN_B5,1);      // destello LED LINK

   if(kbhit())
    {                        
      caracter=getch();
      switch (caracter)         // menu 
      {
          case '1':
            printf("\n\rCONTROL DE POSICION  -  POTENCIOMETRO\n\r");
            modo=4;
         // Asigno el controlador POS
            if((param==0)|(param==2))         //mantenemos el controlador disenado
                  bpos[0]=B0_pos;    
               if(param==1)                  //cambio  del controlador
                  {  bpos[0]=FP(b0,Q);
                     bpos[0]=bpos[0]/RES;
                     }
               bpos[1]=FP(0,Q);
               apos[0]=FP(0,Q);

         RefPos();
            break;
            
            
         case '2':
            printf("\n\rCONTROL DE POSICION  -  TECLADO < Rango: 30* @ 330* >\n\r");
            modo=2;
            gets(texto);                           // Espera una palabra de hasta 10 caracteres
            printf("Referencia POS: %s ",texto);   // Regresa el valor para corroborar que lo tomo bien  
            valor=atoi32(texto);                   // Convierte los caracteres en un numero
            numero=valor;
            posrefgr=valor;
         ContPos();
            break;
            
            
         case '3':
            printf("\n\rCONTROL DE VELOCIDAD  -  POTENCIOMETRO\n\r");
            modo=3;
         // Asigno el controlador VEL
          if((param==0)|(param==1))          // mantenemos el controlador disenado
            {  bvel[0]=B0_vel;  
               bvel[1]=B1_vel; 
            }
          if(param==2)                   // cambio  del controlador
            {  bvel[0]=FP(b0,Q);
                bvel[0]=bvel[0]/RES;
               bvel[1]=FP(b1,Q);
               bvel[1]=bvel[1]/RES;
            }
           avel[0]=FP(1,Q);
             avel[1]=FP(-1,Q);

         RefVel();
            break;
            
   
         case '4':
            printf("\n\rCONTROL DE VELOCIDAD  -  TECLADO < Rango: (-64rpm @ 64rpm) x 32>\n\r");
            modo=1;
            gets(texto);                           // Espera una palabra de hasta 10 caracteres
            printf("Referencia VEL: %s ",texto);   // Regresa el valor para corroborar que lo tomo bien
            valor=atoi32(texto);                   // Convierte los caracteres en un numero
            numero=valor;
            velrefrpm=valor;
         ContVel();
            break;
            
            
         case '5':
            param=1;
            printf("\n\r\nParametros de posicion afectados x 100 para tener mas resolucion:\n\r");
            printf("\n\r   > Introduzca B0\n\r");
            gets(texto); 
            b0=atoi32(texto);
            printf("\n\r   Nuevo valor de B0 x 100 = %ld \r",b0); 
            break;
            
            
         case '6':
            param=2;
            printf("\n\r\nParametros de velocidad afectados x 100 para tener mas resolucion:\n\r");
            printf("\n\r   > Introduzca B0\n\r");
            gets(texto); 
            b0=atoi32(texto);
            printf("\n\r    Nuevo valor de B0 x 100 = %ld \r",b0); 

            printf("\n\r   > Introduzca B1\n\r");
            gets(texto); 
            b1=atoi32(texto);
            printf("\n\r    Nuevo valor de B1 x 100 = %ld \r",b1); 
            break;
            
            
         case '7':
            param=0;
            printf("\n\rParametros de los controladores por defecto.");
            break;
            
            
        default: 
            printf("\n\Menu:");   // MENU QUE SE DESPLIEGA POR DEFAULT AL NO RECIBIR
            printf("\n\r   '1' POS -> POTE");      // LAS TECLAS ANTERIORES
            printf("\n\r   '2' POS -> TECLADO");
            printf("\n\r   '3' VEL -> POTE");
            printf("\n\r   '4' VEL -> TECLADO\n\r");
            printf("\n\r   '5' Edicion Controlador POS");
            printf("\n\r   '6' Edicion Controlador VEL");
            printf("\n\r   '7' Controlador por defecto");

      }

   }   
   output_bit(PIN_B5,0);      // destello LED LINK
   return;
}


// IRQ AD: Fin de conversion AD
// =======
#int_AD
void AD(void)
{  
   switch(modo)
   {   
     case 1:   // presiono "4"
         velocidad();
            output_bit(PIN_B1,0);
            output_bit(PIN_B2,0);
            output_bit(PIN_B3,0);
            output_bit(PIN_B4,1);
            output_bit(PIN_B5,0);  
      break;


      case 2:   // presiono "2"
         posicion(); 
            output_bit(PIN_B1,0);
            output_bit(PIN_B2,1);
            output_bit(PIN_B3,0);
            output_bit(PIN_B4,0);
            output_bit(PIN_B5,0);
      break;


      case 3:  // presiono "3"
         RefVel();
            output_bit(PIN_B1,0);
            output_bit(PIN_B2,0);
            output_bit(PIN_B3,1);
            output_bit(PIN_B4,0);
            output_bit(PIN_B5,0);
      break;


      case 4:  // presiono "1"
         RefPos();
            output_bit(PIN_B1,1);
            output_bit(PIN_B2,0);
            output_bit(PIN_B3,0);
            output_bit(PIN_B4,0);
            output_bit(PIN_B5,0);
      break;
   }
}


// main: programa principal, iniciliza el dispositivo y queda operando por IRQs
// =====
//
// Recibe: nada
// Retorna: nada
void main(void)
{
 setup_psp(PSP_DISABLED);
 setup_spi(SPI_SS_DISABLED);

 // Inicializa el conversor A/D
 setup_adc_ports(AN0_AN1_AN2_AN3_AN4);   
 setup_adc(ADC_CLOCK_DIV_32);      
                                    
 // Configuro el CCP2 para usar con el Timer1
 setup_ccp2(CCP_COMPARE_RESET_TIMER);   
   
 setup_timer_0(RTCC_INTERNAL|RTCC_DIV_32);      //Timer 0 de 4ms
 set_timer0(6);                              //refresco displays

 // Config Timer1 -> Muestreo
 // impongo cada T=10ms  // PS=1 ; TMR1= 0 
 setup_timer_1(T1_INTERNAL|T1_DIV_BY_1);   //Timer 1
 set_timer1(0);                            // TMR1 (16 bits)
      
 CCP_2=20000;                  //muestreo cada 10mseg
  
 ///timer 2 para PWM 
 // prescaler=16, PR2=249, postscaler=1 (no se emplea en Timer2) 
 setup_timer_2(T2_DIV_BY_16,249,1);      
 setup_ccp1(CCP_PWM);            //tPWM= 2ms
   
 setup_comparator(NC_NC_NC_NC);
 setup_vref(FALSE);
    
 enable_interrupts(INT_RDA);            // Habilitamos IRQs
 enable_interrupts(global); 
 enable_interrupts(INT_TIMER0);
 enable_interrupts(INT_AD);   


 //condiguramos los puertos B D y E como salidas
 TRISB= 0x01;         //LEDS
 TRISC= 0x80;
 TRISE= 0x00;         //seccion de uni dec cen
 TRISD= 0x00;         //DISPLAY

 //Se manda 0 por el PORTB al principio
 PORTB=0x01;
 PORTD=0x00;
 PORTE=0x00; 

 //Inicializamos nulos los vectores de texto
 texto[0]=0;
 text1[0]=0;
 text2[0]=0;
 text3[0]=0;
 modo=0;
 numero=0;

 //Inicializamos vacias las colas del controlador
 ini_cola(erropos,MPOS+1);
 ini_cola(errovel,MVEL+1);
 ini_cola(ukpos,MPOS+1);
 ini_cola(ukvel,MVEL+1);

 // MENU INICIAL        
 printf("\n\Menu:            VERSION: g04");   
 printf("\n\r   '1' POS -> POTE");     
 printf("\n\r   '2' POS -> TECLADO");
 printf("\n\r   '3' VEL -> POTE");
 printf("\n\r   '4' VEL -> TECLADO\n\r");
 printf("\n\r   '5' Edicion Controlador POS");
 printf("\n\r   '6' Edicion Controlador VEL");
 printf("\n\r   '7' Controlador por defecto");

 set_pwm1_duty(512);      // Inicializo detenido el motor

 while(1)
   {   
    display_FP(numero);
   }
}
