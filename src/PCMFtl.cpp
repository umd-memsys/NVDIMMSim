//PCMFtl.cpp
//class file for PCMftl
//
#include "GCFtl.h"
#include "ChannelPacket.h"
#include <cmath>

using namespace NVDSim;
using namespace std;

PCMFtl::PCMFtl(Controller *c)
  : Ftl(c)
{

	vpp_idle_energy = vector<double>(NUM_PACKAGES, 0.0); 
	vpp_access_energy = vector<double>(NUM_PACKAGES, 0.0); 

}

void PCMFtl::update(void){
        uint64_t block, page, start;
	if (busy) {
		if (lookupCounter == 0){
			uint64_t vAddr = currentTransaction.address, pAddr;
			bool done = false;
			ChannelPacket *commandPacket, *dataPacket;
			
			switch (currentTransaction.transactionType){
				case DATA_READ:
					if (addressMap.find(vAddr) == addressMap.end()){
						controller->returnReadData(FlashTransaction(RETURN_DATA, vAddr, (void *)0xdeadbeef));
					} else {
						commandPacket = Ftl::translate(READ, vAddr, addressMap[vAddr]);
						controller->addPacket(commandPacket);
						//update access energy figures
						access_energy[commandPacket->package] += (READ_I - STANDBY_I) * READ_TIME/2;
						//update access energy figure with PCM stuff (if applicable)
						vpp_access_energy[commandPacket->package] += (VPP_READ_I - VPP_STANDBY_I) * READ_TIME/2;
					}
					break;
				case DATA_WRITE:
				        if (addressMap.find(vAddr) != addressMap.end()){
					  // we're going to write this data somewhere else for wear-leveling purposes however we will probably 
					  // want to reuse this block for something at some later time so mark it as unused because it is
					   used[addressMap[vAddr] / BLOCK_SIZE][(addressMap[vAddr] / NV_PAGE_SIZE) % PAGES_PER_BLOCK] = false;
					}			          
					//look for first free physical page starting at the write pointer
	                                start = BLOCKS_PER_PLANE * (plane + PLANES_PER_DIE * (die + NUM_PACKAGES * channel));

					for (block = start ; block < TOTAL_SIZE / BLOCK_SIZE && !done; block++){
					  for (page = 0 ; page < PAGES_PER_BLOCK  && !done ; page++){
						if (!used[block][page]){
						        pAddr = (block * BLOCK_SIZE + page * NV_PAGE_SIZE);
							used[block][page] = true;
							used_page_count++;
							done = true;
						}
					  }
					}
					


					//if we didn't find a free page after scanning til the end, check the beginning
				        if (!done){
					  for (block = 0 ; block < start / BLOCK_SIZE && !done ; block++){
					      for (page = 0 ; page < PAGES_PER_BLOCK && !done ; page++){
						if (!used[block][page]){
							pAddr = (block * BLOCK_SIZE + page * NV_PAGE_SIZE);
					  		used[block][page] = true;
							used_page_count++;
							done = true;		 
					        }
					      }
					  }								
					}

					if (!done){
						// TODO: Call GC
						ERROR("No free pages? GC needs some work.");
						exit(1);
					} else {
						addressMap[vAddr] = pAddr;
					}
					//send write to controller
					dataPacket = Ftl::translate(DATA, vAddr, pAddr);
					commandPacket = Ftl::translate(WRITE, vAddr, pAddr);
					controller->addPacket(dataPacket);
					controller->addPacket(commandPacket);
					//update "write pointer"
					channel = (channel + 1) % NUM_PACKAGES;
					if (channel == 0){
						die = (die + 1) % DIES_PER_PACKAGE;
						if (die == 0)
							plane = (plane + 1) % PLANES_PER_DIE;
					}
					//update access energy figures
					access_energy[commandPacket->package] += (WRITE_I - STANDBY_I) * WRITE_TIME/2;
					//update access energy figure with PCM stuff (if applicable)
					vpp_access_energy[commandPacket->package] += (VPP_WRITE_I - VPP_STANDBY_I) * WRITE_TIME/2;
					break;

				case BLOCK_ERASE:
				        ERROR("Called Block erase on PCM memory which does not need this");
					break;				
				default:
					ERROR("Transaction in Ftl that isn't a read or write... What?");
					exit(1);
					break;
			}
			transactionQueue.pop_front();
			busy = 0;
		} 
		else
			lookupCounter--;
	} // if busy
	else {
		// Not currently busy.
		if (!transactionQueue.empty()) {
			busy = 1;
			currentTransaction = transactionQueue.front();
			lookupCounter = LOOKUP_TIME;
		}
	}

	//update idle energy
	//since this is already subtracted from the access energies we just do it every time
	for(uint i = 0; i < (NUM_PACKAGES); i++)
	{
	  idle_energy[i] += STANDBY_I;
	  vpp_idle_energy{i} += VPP_STANDBY_I;
	}

	//place power callbacks to hybrid_system
#if Verbose_Power_Callback
	  controller->returnPowerData(idle_energy, access_energy);
#endif

}

vector<double> PCMFtl::getVppIdleEnergy(void) {
  return idle_energy;
}

vector<double> PCMFtl::getVppAccessEnergy(void) {
  return access_energy;
}


