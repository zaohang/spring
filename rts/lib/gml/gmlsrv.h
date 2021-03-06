// GML - OpenGL Multithreading Library
// for Spring http://springrts.com
// Author: Mattias "zerver" Radeskog
// (C) Ware Zerver Tech. http://zerver.net
// Ware Zerver Tech. licenses this library
// to be used, distributed and modified 
// freely for any purpose, as long as 
// this notice remains unchanged

#ifndef GMLSRV_H
#define GMLSRV_H

#ifdef USE_GML
#include "System/OffscreenGLContext.h"
#include <boost/thread/thread.hpp>
#include <boost/thread/barrier.hpp>
#include <boost/bind.hpp>
#include "System/Platform/errorhandler.h"
#include "System/Platform/Watchdog.h"
#include "lib/streflop/streflop_cond.h"
#if !defined(_MSC_VER) && defined(_WIN32)
#	include "System/Platform/Win/win32.h"
#endif

extern COffscreenGLContext* ogc[GML_MAX_NUM_THREADS];

EXTERN inline void gmlUpdateServers() {
	gmlItemsConsumed=0;
	gmlProgramServer.GenerateItems();
	gmlProgramObjectARBServer.GenerateItems();
	gmlShaderServer_VERTEX.GenerateItems();
	gmlShaderServer_FRAGMENT.GenerateItems();
	gmlShaderServer_GEOMETRY_EXT.GenerateItems();
	gmlShaderObjectARBServer_VERTEX.GenerateItems();
	gmlShaderObjectARBServer_FRAGMENT.GenerateItems();
	gmlShaderObjectARBServer_GEOMETRY_EXT.GenerateItems();
	gmlQuadricServer.GenerateItems();

	gmlTextureServer.GenerateItems();
	gmlBufferARBServer.GenerateItems();
	gmlFencesNVServer.GenerateItems();
	gmlProgramsARBServer.GenerateItems();
	gmlRenderbuffersEXTServer.GenerateItems();
	gmlFramebuffersEXTServer.GenerateItems();
	gmlQueryServer.GenerateItems();
	gmlBufferServer.GenerateItems();

	gmlListServer.GenerateItems();
}

#define GML_MAX_EXEC_DEPTH 4

template<class R, class A, typename U>
struct gmlExecState {
	R (*worker)(void *);
	R (*workerarg)(void *,A);
	R (*workeriter)(void *,U);
	void* workerclass;
	int maxthreads;
	BOOL_ syncmode;
	int num_units;
	const GML_TYPENAME std::set<U> *iter;
	int limit1;
	int limit2;
	BOOL_ serverwork;
	void (*serverfun)(void *);

	gmlCount UnitCounter;
	gmlExecState(R (*wrk)(void *)=NULL,R (*wrka)(void *,A)=NULL,R (*wrki)(void *,U)=NULL,
		void* cls=NULL,int mt=0,BOOL_ sm=FALSE,int nu=0,const GML_TYPENAME std::set<U> *it=NULL,int l1=1,int l2=1,BOOL_ sw=FALSE,void (*swf)(void *)=NULL):
	worker(wrk),workerarg(wrka),workeriter(wrki),workerclass(cls),maxthreads(mt),
		syncmode(sm),num_units(nu),iter(it),limit1(l1),limit2(l2),serverwork(sw),serverfun(swf),UnitCounter(-1) {
	}

	void ExecServerFun() {
		if(serverfun)
			(*serverfun)(workerclass);
	}

	void ExecAll(int &pos, typename std::set<U>::const_iterator &it) {
		int i=UnitCounter;
		if(i>=num_units)
			return;
		if(workeriter) {
			while(++i<num_units) {
				(*workeriter)(workerclass,*it);
				++it;
				++pos;
			}
		}
		else if(worker) {
			while(++i<num_units)
				(*worker)(workerclass);
		}
		else if(workerarg) {
			while(++i<num_units)
				(*workerarg)(workerclass,i);
		}
		UnitCounter%=num_units;
	}

	BOOL_ Exec(int &pos, typename std::set<U>::const_iterator &it) {
		int i=++UnitCounter;
		if(i>=num_units)
			return FALSE;
		if(workeriter) {
			while(pos<i) {
				++it;
				++pos;
			}
			(*workeriter)(workerclass,*it);
		}
		else if(worker) {
			(*worker)(workerclass);
		}
		else if(workerarg) {
			(*workerarg)(workerclass,i);
		}
		return TRUE;
	}

};


template<class R,class A, typename U>
class gmlClientServer {
public:
	int ExecDepth;
	GML_TYPENAME gmlExecState<R,A,U> ExecState[GML_MAX_EXEC_DEPTH];
	boost::barrier Barrier; 
	boost::thread *threads[GML_MAX_NUM_THREADS];
	volatile BOOL_ dorun;
	BOOL_ inited;
	gmlCount threadcnt;
	gmlCount ClientsReady;
	BOOL_ newwork;

	BOOL_ auxinited;
	R (*auxworker)(void *);
	void* auxworkerclass;
	boost::barrier AuxBarrier; 
	gmlCount AuxClientsReady;


	gmlClientServer()
		: ExecDepth(0)
		, Barrier(GML_CPU_COUNT)
		, dorun(TRUE)
		, inited(FALSE)
		, threadcnt(0)
		, ClientsReady(0)
		, newwork(FALSE)
		, auxinited(FALSE)
		, auxworker(NULL)
		, auxworkerclass(NULL)
		, AuxBarrier(2)
		, AuxClientsReady(0) {
	}

	~gmlClientServer() {
		if(inited) {
			GML_TYPENAME gmlExecState<R,A,U> *ex=ExecState+ExecDepth;
			BOOL_ dowait=TRUE;
			for(int i=3; i<=gmlThreadCount+1; ++i) {
				if(!threads[i]->joinable() || threads[i]->timed_join(boost::posix_time::milliseconds(10)))
					dowait=FALSE;
			}
			ex->maxthreads=0;
			dorun=FALSE;
			if(dowait)
				Barrier.wait();
			for(int i=3; i<=gmlThreadCount+1; ++i) {
				if(threads[i]->joinable()) {
					if(dowait)
						threads[i]->join();
					else if(!threads[i]->timed_join(boost::posix_time::milliseconds(100)))
						threads[i]->interrupt();
				}
				delete threads[i];
			}
		}
		if(auxinited) {
			boost::thread *simthread = threads[GML_SIM_THREAD_NUM];
			BOOL_ dowait=simthread->joinable() && !simthread->timed_join(boost::posix_time::milliseconds(10));
			auxworker=NULL;
			dorun=FALSE;
			if(dowait)
				AuxBarrier.wait();
			if(simthread->joinable()) {
				if(dowait)
					simthread->join();
				else if(!simthread->timed_join(boost::posix_time::milliseconds(100)))
					simthread->interrupt();
			}
			delete simthread;
		}
	}

	void gmlServer() {

		do {
			ClientsReady%=0;
			if(newwork>0)
				++ExecDepth;
			GML_TYPENAME gmlExecState<R,A,U> *ex=ExecState+ExecDepth;
			if(newwork==0)
				ex->UnitCounter%=-1;
			BOOL_ execswf=newwork>=0;
			newwork=0;

			Barrier.wait();
			
			if(execswf)
				ex->ExecServerFun();

			typename std::set<U>::const_iterator it;
			if(ex->workeriter)
				it=ex->iter->begin();
			int pos=0;
//			int nproc=0;
			int updsrv=0;
			if(ex->maxthreads>1) {
				while(ClientsReady < gmlThreadCount - 1) {
					if(!gmlShareLists && ((updsrv++%GML_UPDSRV_INTERVAL)==0 || *(volatile int *)&gmlItemsConsumed>=GML_UPDSRV_INTERVAL))
						gmlUpdateServers();
					BOOL_ processed=FALSE;

					for(int i=3; i<=gmlThreadCount+1; ++i) {
						gmlQueue *qd=&gmlQueues[i];
						if(qd->Reloc)
							qd->Realloc();
						if(qd->GetRead()) {
							qd->Execute();
							qd->ReleaseRead();
							processed=TRUE;
						}
						if(qd->Sync) {
							qd->ExecuteSynced();
							processed=TRUE;
						}
					}
					if(GML_SERVER_GLCALL && ex->serverwork && !processed) {
						if(ex->Exec(pos,it)) {
							//						++nproc;
						}
					}
				}
			}
			else {
				ex->ExecAll(pos,it);
			}

//			GML_DEBUG("server ",nproc, 3)
			if(ExecDepth>0 && !*(volatile int *)&newwork) {
				--ExecDepth;
				newwork=-1;
			}

		} while(*(volatile int *)&newwork);
	}

	void WorkInit() {
//		set_threadnum(0);
		gmlInit();

		for(int i=3; i<=gmlThreadCount+1; ++i)
			threads[i]=new boost::thread(boost::bind<void, gmlClientServer, gmlClientServer*>(&gmlClientServer::gmlClient, this));
#if GML_ENABLE_TLS_CHECK
		for(int i=0; i<GML_MAX_NUM_THREADS; ++i)
			boost::thread::yield();
		if(gmlThreadNumber != GML_DRAW_THREAD_NUM) {
			handleerror(NULL, "Thread Local Storage test failed", "GML error:", MBF_OK | MBF_EXCL);
		}
#endif
		inited=TRUE;
	}

	void Work(R (*wrk)(void *),R (*wrka)(void *,A), R (*wrkit)(void *,U),void *cls,int mt,BOOL_ sm, const GML_TYPENAME std::set<U> *it,int nu,int l1,int l2,BOOL_ sw,void (*swf)(void *)=NULL) {
		if(!inited)
			WorkInit();
		if(auxworker)
			--mt;
		if(gmlThreadNumber != GML_DRAW_THREAD_NUM) {
			NewWork(wrk,wrka,wrkit,cls,mt,sm,it,nu,l1,l2,sw,swf);
			return;
		}
		GML_TYPENAME gmlExecState<R,A,U> *ex=ExecState;
		new (ex) GML_TYPENAME gmlExecState<R,A,U>(wrk,wrka,wrkit,cls,mt,sm,nu,it,l1,l2,sw,swf);
		gmlServer();
	}

	void NewWork(R (*wrk)(void *),R (*wrka)(void *,A), R (*wrkit)(void *,U),void *cls,int mt,BOOL_ sm, const GML_TYPENAME std::set<U> *it,int nu,int l1,int l2,BOOL_ sw,void (*swf)(void *)=NULL) {
		gmlQueue *qd=&gmlQueues[gmlThreadNumber];
		qd->ReleaseWrite();

		GML_TYPENAME gmlExecState<R,A,U> *ex=ExecState+ExecDepth;
		new (ex+1) GML_TYPENAME gmlExecState<R,A,U>(wrk,wrka,wrkit,cls,mt,sm,nu,it,l1,l2,sw,swf);
		newwork=TRUE;

		while(!qd->Empty())
			boost::thread::yield();
		++ClientsReady;	
		gmlClientSub();

		Barrier.wait();

		qd->GetWrite(TRUE);
		if(ex->syncmode)
			qd->SyncRequest();

	}

	void gmlClientSub() {
		Barrier.wait();

		GML_TYPENAME gmlExecState<R,A,U> *ex=ExecState+ExecDepth;
		
		int thread=gmlThreadNumber;
		if(thread>=ex->maxthreads+2) {
			++ClientsReady;	
			return;
		}

		typename std::set<U>::iterator it;
		if(ex->workeriter)
			it=((GML_TYPENAME std::set<U> *)*(GML_TYPENAME std::set<U> * volatile *)&ex->iter)->begin();
		int pos=0;

		int processed=0;
		gmlQueue *qd=&gmlQueues[thread];

		qd->GetWrite(TRUE); 

		if(ex->syncmode)
			qd->SyncRequest();

		while(ex->Exec(pos,it)) {
			++processed;

//			int exproc=processed;
#if GML_ALTERNATE_SYNCMODE
			if(qd->WasSynced && qd->GetWrite(ex->syncmode?TRUE:2))
#else
			if(qd->WasSynced && qd->GetWrite(TRUE))
#endif
				processed=0;
			if(processed>=ex->limit1 && qd->GetWrite(processed>=ex->limit2))
				processed=0;
//			if(exproc!=processed) {
//				GML_DEBUG("client ",exproc, 3)
//			}
		}
		qd->ReleaseWrite();
		while(!qd->Empty())
			boost::thread::yield();
		++ClientsReady;	
	}

	void gmlClient() {
		long thr = ++threadcnt;
		set_threadnum(thr + 2);
		if (gmlShareLists) {
			ogc[thr]->WorkerThreadPost();
		}
		streflop::streflop_init<streflop::Simple>();
		while(dorun) {
			gmlClientSub();
		}
		if (gmlShareLists) {
			ogc[thr]->WorkerThreadFree();
		}
	}

	void GetQueue() {
		gmlQueue *qd=&gmlQueues[GML_SIM_THREAD_NUM];

		if(!qd->WasSynced && qd->Write==qd->WritePos)
			return;

		BOOL_ isq1=qd->Write==qd->Queue1;

		qd->GetWrite(qd->WasSynced?2:TRUE);

		if(isq1) {
			while(!qd->Locked1 && !qd->Empty(1))
				boost::thread::yield();
		}
		else {
			while(!qd->Locked2 && !qd->Empty(2))
				boost::thread::yield();
		}
	}

	BOOL_ PumpAux() {
		if(!threads[GML_SIM_THREAD_NUM]->joinable())
			return TRUE;
		static int updsrvaux=0;
		if(!gmlShareLists && ((updsrvaux++%GML_UPDSRV_INTERVAL)==0 || *(volatile int *)&gmlItemsConsumed>=GML_UPDSRV_INTERVAL))
			gmlUpdateServers();

		while(AuxClientsReady<=3) {
			gmlQueue *qd=&gmlQueues[GML_SIM_THREAD_NUM];
			if(qd->Reloc)
				qd->Realloc();
			if(qd->GetRead()) {
				qd->ExecuteDebug();
				qd->ReleaseRead();
			}
			if(qd->Sync) {
				qd->ExecuteSynced(&gmlQueue::ExecuteDebug);
			}
			if(AuxClientsReady==0)
				return FALSE;
			else
				++AuxClientsReady;
		}
		return TRUE;
	}

	void AuxWork(R (*wrk)(void *),void *cls) {
		auxworker=wrk;
		auxworkerclass=cls;
		AuxClientsReady%=0;
		if(!auxinited) {
			if(!inited)
				WorkInit();
			threads[GML_SIM_THREAD_NUM]=new boost::thread(boost::bind<void, gmlClientServer, gmlClientServer*>(&gmlClientServer::gmlClientAux, this));
			auxinited=TRUE;
		}
		AuxBarrier.wait();
	}


	void gmlClientAuxSub() {
		AuxBarrier.wait();

		if(!auxworker)
			return;
		
		gmlQueue *qd=&gmlQueues[GML_SIM_THREAD_NUM];

		qd->GetWrite(TRUE); 

		(*auxworker)(auxworkerclass);

		qd->ReleaseWrite();

		++AuxClientsReady;
		auxworker=NULL;
	}

	void gmlClientAux() {
		Threading::SetThreadName("sim");
		Watchdog::RegisterThread(WDT_SIM, true);
		set_threadnum(GML_SIM_THREAD_NUM);
		streflop::streflop_init<streflop::Simple>();
		while(dorun) {
			gmlClientAuxSub();
		}
	}

	void ExpandAuxQueue() {
		gmlQueue *qd=&gmlQueues[gmlThreadNumber];
		while(qd->WriteSize<qd->Write+GML_AUX_PREALLOC)
			qd->WaitRealloc();
	}

};

#endif // USE_GML

#endif
