/*
Minetest
Copyright (C) 2013 sapier, <sapier AT gmx DOT net>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include <vector>
#include <deque>
#include <unordered_set>
#include <memory>

#include <lua.h>
#include "threading/semaphore.h"
#include "threading/thread.h"
#include "common/c_packer.h"
#include "cpp_api/s_base.h"
#include "cpp_api/s_security.h"

// Forward declarations
class AsyncEngine;


// Declarations

// Data required to queue a job
struct LuaJobInfo
{
	LuaJobInfo() = default;

	// Function to be called in async environment (from string.dump)
	std::string function;
	// Parameter to be passed to function (serialized)
	std::string params;
	// Alternative parameters
	std::unique_ptr<PackedValue> params_ext;
	// Result of function call (serialized)
	std::string result;
	// Alternative result
	std::unique_ptr<PackedValue> result_ext;
	// Name of the mod who invoked this call
	std::string mod_origin;
	// JobID used to identify a job and match it to callback
	u32 id;
};

// Asynchronous working environment
class AsyncWorkerThread : public Thread,
	virtual public ScriptApiBase, public ScriptApiSecurity {
	friend class AsyncEngine;
public:
	virtual ~AsyncWorkerThread();

	void *run();

protected:
	AsyncWorkerThread(AsyncEngine* jobDispatcher, const std::string &name);

private:
	AsyncEngine *jobDispatcher = nullptr;
	bool isErrored = false;
};

// Asynchornous thread and job management
class AsyncEngine {
	friend class AsyncWorkerThread;
	typedef void (*StateInitializer)(lua_State *L, int top);
public:
	AsyncEngine() = default;
	AsyncEngine(Server *server) : server(server) {};
	~AsyncEngine();

	/**
	 * Register function to be called on new states
	 * @param func C function to be called
	 */
	void registerStateInitializer(StateInitializer func);

	/**
	 * Create async engine tasks and lock function registration
	 * @param numEngines Number of worker threads, 0 for automatic scaling
	 */
	void initialize(unsigned int numEngines);

	/**
	 * Queue an async job
	 * @param func Serialized lua function
	 * @param params Serialized parameters
	 * @return jobid The job is queued
	 */
	u32 queueAsyncJob(std::string &&func, std::string &&params,
			const std::string &mod_origin = "");

	/**
	 * Queue an async job
	 * @param func Serialized lua function
	 * @param params Serialized parameters (takes ownership!)
	 * @return ID of queued job
	 */
	u32 queueAsyncJob(std::string &&func, PackedValue *params,
			const std::string &mod_origin = "");

	/**
	 * Engine step to process finished jobs
	 * @param L The Lua stack
	 */
	void step(lua_State *L);

protected:
	/**
	 * Get a Job from queue to be processed
	 *  this function blocks until a job is ready
	 * @param job a job to be processed
	 * @return whether a job was available
	 */
	bool getJob(LuaJobInfo *job);

	/**
	 * Put a Job result back to result queue
	 * @param result result of completed job
	 */
	void putJobResult(LuaJobInfo &&result);

	/**
	 * Start an additional worker thread
	 */
	void addWorkerThread();

	/**
	 * Process finished jobs callbacks
	 */
	void stepJobResults(lua_State *L);

	/**
	 * Handle automatic scaling of worker threads
	 */
	void stepAutoscale();

	/**
	 * Initialize environment with current registred functions
	 *  this function adds all functions registred by registerFunction to the
	 *  passed lua stack
	 * @param L Lua stack to initialize
	 * @param top Stack position
	 * @return false if a mod error ocurred
	 */
	bool prepareEnvironment(lua_State* L, int top);

private:
	// Variable locking the engine against further modification
	bool initDone = false;

	// Maximum number of worker threads for automatic scaling
	// 0 if disabled
	unsigned int autoscaleMaxWorkers = 0;
	u64 autoscaleTimer = 0;
	std::unordered_set<u32> autoscaleSeenJobs;

	// Only set for the server async environment (duh)
	Server *server = nullptr;
	
	// Internal store for registred state initializers
	std::vector<StateInitializer> stateInitializers;

	// Internal counter to create job IDs
	u32 jobIdCounter = 0;

	// Mutex to protect job queue
	std::mutex jobQueueMutex;
	// Job queue
	std::deque<LuaJobInfo> jobQueue;

	// Mutex to protect result queue
	std::mutex resultQueueMutex;
	// Result queue
	std::deque<LuaJobInfo> resultQueue;

	// List of current worker threads
	std::vector<AsyncWorkerThread*> workerThreads;

	// Counter semaphore for job dispatching
	Semaphore jobQueueCounter;
};
