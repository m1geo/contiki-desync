/*
 * DESYNC.c
 * 
 * George Smart <g.smart@ee.ucl.ac.uk> (PhD Student)
 * Yiannis Andreopoulos <iandreop@ee.ucl.ac.uk> (Supervisor)
 * 
 * Friday 01 August 2014.
 * 
 * Telecommunications Research Group Office, Room 804, 
 * Roberts Buidling, Department of Electronic & Electrical Engineering
 * University College London
 * Malet Place, London, WC1E 7JE, United Kingdom.
 * 
 */


/**
 * \file
 *         Desync Algorithm Test
 * \author
 *         George Smart <g.smart@ee.ucl.ac.uk>
 */


//
//  TODO FIXME GSHACK  For some reason, there needs to be at least 3 nodes per channel!
//
//	CHECK THAT BEACONS WERE HEARD BEFORE RUNNING DESYNC!
//
//  
//
//  VERY IMPORTANT:  CHANGED rtimer_set() INSIDE rtimer.c FILE
//                   *** REMOVED "if(first == 1)" CONDITIONAL ***
//
//  to become:  	//if(first == 1) {
//			    		rtimer_arch_schedule(time);
//  				//}
//

#define rtimer_set_george	rtimer_set

#define PRINTF(...) printf(__VA_ARGS__)
//#define PRINTF(...) do {} while (0)


#include "contiki.h"
#include "net/rime.h"
#include "random.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include "sys/rtimer.h"
#include "dev/cc2420.h"
#include "net/netstack.h"
#include <stdio.h>


#define RADIOPWR   CC2420_TXPOWER_MAX	// Between 1 and 31.  Min to Max. CC2420_TXPOWER_MIN / CC2420_TXPOWER_MAX
const unsigned short int channel = 11;
const double alpha = 0.6; //0.9 in paper, 0.6 works nicely on hardware/simulator.
unsigned short sJustFired = 0;
struct rtimer desynctimer;


rtimer_clock_t  rtPERIOD = ((RTIMER_SECOND/10)-80);  // approx 100ms (80 is code delay)
rtimer_clock_t  tNextFire = 0;
rtimer_clock_t  tPrevFire = 0;
rtimer_clock_t  tFire = 0;
rtimer_clock_t  tNext = 0;
rtimer_clock_t  tPrev = 0;


PROCESS(example_desync_process, "DESYNC Example");
AUTOSTART_PROCESSES(&example_desync_process);


static void FireCallback (void *ptr);
static void broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from);
static rtimer_clock_t CheckConvergence(rtimer_clock_t tCur, rtimer_clock_t tPre);
static void calculateFireTimer(void);


static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;


static void
calculateFireTimer(void)
{
	rtimer_clock_t  tempP = 0;
	rtimer_clock_t  tempC = 0;
	rtimer_clock_t  tempN = 0;
	
	// move tPrev to 0
	if (tNext > tPrev) {
		tempC = tFire - tPrev;
		tempN = tNext - tPrev;
		tempP = 0;
	} else { // overflowed
		tempC = tFire + (65535 - tPrev) + 0;
		tempN = tNext + (65535 - tPrev) + 0;
		tempP = 0;
	}

	// update equation
	tNextFire = rtPERIOD + (1.0-alpha) * tempC + alpha * ( (tempP/2) + (tempN/2) );
	
	// rescale to abosolute tPrev
	tNextFire += tPrev;
	
	// check that the new firing is within 1 period, else, we reset the value to fire on T
	if ((tNextFire-tFire) <= ((1+alpha)*((double)rtPERIOD))) {
		rtimer_set_george(&desynctimer, tNextFire, 1, (rtimer_callback_t) FireCallback, NULL);
	} else {
		tNextFire = tFire + rtPERIOD;
	}
	
	printf("Interfiring Time = %05u\n", CheckConvergence(tFire, tPrevFire));
}


static void
broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from)
{
	if (sJustFired > 0) {
		sJustFired = 0;
		tNext = RTIMER_NOW();
		calculateFireTimer();
		tPrev = tNext;
	} else {
		tPrev = RTIMER_NOW();
	}
}


static rtimer_clock_t
CheckConvergence(rtimer_clock_t tCur, rtimer_clock_t tPre)
{	
	rtimer_clock_t diff = 0;
	if (tCur < tPre) { // rtimer overflowed
		diff = 65535 - tPre + tCur;
	} else {
		diff = tCur - tPre;
	}
	return diff;
}


static void
FireCallback (void *ptr)
{	
	tPrevFire = tFire;
	packetbuf_copyfrom("DESYNC", 7);  // 7 bytes of beacon
	broadcast_send(&broadcast); // Transmit beacon.
	tFire = RTIMER_NOW();
	tNextFire = tFire + rtPERIOD;
	sJustFired = 1;
	rtimer_set_george(&desynctimer, tNextFire, 1, (rtimer_callback_t) FireCallback, NULL);
}


PROCESS_THREAD(example_desync_process, ev, data)
{
	PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
	PROCESS_BEGIN();
  	
  	// start the realtime scheduler
	rtimer_init();
	
	// Seed the RNG with our node address
	random_init(rimeaddr_node_addr.u8[0]+rimeaddr_node_addr.u8[1]);
	
	printf("Node (%d.%d) on Channel %u\n",  rimeaddr_node_addr.u8[0], rimeaddr_node_addr.u8[1], channel);
	printf("Compilation Timestamp %s - %s:  %s\n", __TIME__, __DATE__,  __FILE__);
	printf("Period=%u, RTIMER_SECOND=%u (%u bytes)\n", rtPERIOD, RTIMER_SECOND, sizeof(rtimer_clock_t));
	
	broadcast_open(&broadcast, 129, &broadcast_call);
	
	cc2420_set_channel(channel);

	// start randomly through the period
	rtimer_set_george(&desynctimer, (RTIMER_NOW() + (random_rand() % (rtPERIOD))), 1, (rtimer_callback_t) FireCallback, NULL);

	NETSTACK_MAC.on();
	
	PROCESS_END();
}
