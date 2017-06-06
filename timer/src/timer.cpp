// copyright britzl
// Extension lib defines
#define LIB_NAME "Timer"
#define MODULE_NAME "timer"

// include the Defold SDK
#include <dmsdk/sdk.h>
#include <sys/time.h>

struct Listener {
	Listener() {
		m_L = 0;
		m_Callback = LUA_NOREF;
		m_Self = LUA_NOREF;
	}
	lua_State* m_L;
	int        m_Callback;
	int        m_Self;
};

struct Timer {
	double seconds;
	double end;
	int repeating;
	unsigned int id;
	Listener listener;
};

static unsigned int g_SequenceId = 0;
static const int TIMERS_CAPACITY = 128;
static dmArray<Timer*> g_Timers;

static dmArray<int> g_TimersToTrigger;
static dmArray<int> g_TimersToRemove;


/**
 * Create a listener instance from a function on the stack
 */
static Listener CreateListener(lua_State* L, int index) {
	luaL_checktype(L, index, LUA_TFUNCTION);
	lua_pushvalue(L, index);
	int cb = dmScript::Ref(L, LUA_REGISTRYINDEX);

	Listener listener;
	listener.m_L = dmScript::GetMainThread(L);
	listener.m_Callback = cb;
	dmScript::GetInstance(L);
	listener.m_Self = dmScript::Ref(L, LUA_REGISTRYINDEX);
	return listener;
}

/**
 * Get a timestamp in milliseconds
 */
static double GetTimestamp() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000;
}

/**
 * Create a new timer
 */
static Timer* CreateTimer(Listener listener, double seconds, int repeating) {
	Timer *timer = (Timer*)malloc(sizeof(Timer));
	timer->seconds = seconds;
	timer->id = g_SequenceId++;
	timer->listener = listener;
	timer->repeating = repeating;
	timer->end = GetTimestamp() + seconds * 1000.0f;

	if (g_Timers.Full()) {
		g_Timers.SetCapacity(g_Timers.Capacity() + TIMERS_CAPACITY);
	}
	g_Timers.Push(timer);
	return timer;
}

/**
 * Get a timer
 */
static Timer* GetTimer(int id) {
	for (int i = g_Timers.Size() - 1; i >= 0; i--) {
		Timer* timer = g_Timers[i];
		if (timer->id == id) {
			return timer;
		}
	}
	return 0;
}

/**
 * Create a timer that will trigger after a certain number of seconds
 */
static int Seconds(lua_State* L) {
	int top = lua_gettop(L);

	const double seconds = luaL_checknumber(L, 1);
	const Listener listener = CreateListener(L, 2);

	Timer *timer = CreateTimer(listener, seconds, 0);

	lua_pushinteger(L, timer->id);

	assert(top + 1 == lua_gettop(L));
	return 1;
}

/**
 * Create a timer that will trigger repeatedly with a fixed interval
 */
static int Repeating(lua_State* L) {
	int top = lua_gettop(L);

	const double seconds = luaL_checknumber(L, 1);
	const Listener listener = CreateListener(L, 2);

	Timer *timer = CreateTimer(listener, seconds, 1);

	lua_pushinteger(L, timer->id);

	assert(top + 1 == lua_gettop(L));
	return 1;
}

/**
 * Remove a timer from the list of timers and free it's memory
 */
static void Remove(int id) {
	for (int i = g_Timers.Size() - 1; i >= 0; i--) {
		Timer* timer = g_Timers[i];
		if (timer->id == id) {
			g_Timers.EraseSwap(i);
			free(timer);
			break;
		}
	}
}

/**
 * Cancel a timer
 */
static int Cancel(lua_State* L) {
	int top = lua_gettop(L);

	int id = luaL_checkint(L, 1);

	Remove(id);

	// Remove the timer from the temporary lists used during Update()
	// This could be the case if a finished timer is canceling other
	// timers in it's callback function
	for (int i = g_TimersToRemove.Size() - 1; i >= 0; i--) {
		int id_to_remove = g_TimersToRemove[i];
		if (id_to_remove == id) {
			g_TimersToRemove.EraseSwap(i);
			break;
		}
	}
	for (int i = g_TimersToTrigger.Size() - 1; i >= 0; i--) {
		int id_to_trigger = g_TimersToTrigger[i];
		if (id_to_trigger == id) {
			g_TimersToTrigger.EraseSwap(i);
			break;
		}
	}

	assert(top + 0 == lua_gettop(L));
	return 0;
}

/**
 * Cancel all timers
 */
static int CancelAll(lua_State* L) {
	int top = lua_gettop(L);

	for (int i = g_Timers.Size() - 1; i >= 0; i--) {
		Timer* timer = g_Timers[i];
		g_Timers.EraseSwap(i);
		free(timer);
	}

	// Make sure to clear the temporary lists as well
	// In case of a timer callback calling timer.cancel_all()
	while(!g_TimersToRemove.Empty()) {
		g_TimersToRemove.Pop();
	}
	while(!g_TimersToTrigger.Empty()) {
		g_TimersToTrigger.Pop();
	}

	assert(top + 0 == lua_gettop(L));
	return 0;
}

// Functions exposed to Lua
static const luaL_reg Module_methods[] = {
	{ "seconds", Seconds },
	{ "repeating", Repeating },
	{ "cancel", Cancel },
	{ "cancel_all", CancelAll },
	{ 0, 0 }
};

static void LuaInit(lua_State* L) {
	int top = lua_gettop(L);

	luaL_register(L, MODULE_NAME, Module_methods);

	lua_pop(L, 1);
	assert(top == lua_gettop(L));
}

dmExtension::Result AppInitializeTimerExtension(dmExtension::AppParams* params) {
	return dmExtension::RESULT_OK;
}

dmExtension::Result InitializeTimerExtension(dmExtension::Params* params) {
	LuaInit(params->m_L);
	printf("Registered %s Extension\n", MODULE_NAME);
	return dmExtension::RESULT_OK;
}

dmExtension::Result AppFinalizeTimerExtension(dmExtension::AppParams* params) {
	return dmExtension::RESULT_OK;
}

dmExtension::Result UpdateTimerExtension(dmExtension::Params* params) {
	const double now = GetTimestamp();

	// Iterate over all timers to find the ones that should be triggered
	// and possibly also removed this frame (unless they are repeating)
	for (int i = g_Timers.Size() - 1; i >= 0; i--) {
		Timer* timer = g_Timers[i];
		if (now >= timer->end) {
			if (g_TimersToTrigger.Full()) {
				g_TimersToTrigger.SetCapacity(g_TimersToTrigger.Capacity() + TIMERS_CAPACITY);
			}
			g_TimersToTrigger.Push(timer->id);

			if (timer->repeating == 1) {
				timer->end += timer->seconds * 1000.0f;
			} else {
				if (g_TimersToRemove.Full()) {
					g_TimersToRemove.SetCapacity(g_TimersToRemove.Capacity() + TIMERS_CAPACITY);
				}
				g_TimersToRemove.Push(timer->id);
			}
		}
	}

	// Trigger timer callbacks
	while(!g_TimersToTrigger.Empty()) {
		int id = g_TimersToTrigger.Back();
		g_TimersToTrigger.Pop();
		Timer* timer = GetTimer(id);
		if (timer) {
			lua_State* L = timer->listener.m_L;
			int top = lua_gettop(L);

			lua_rawgeti(L, LUA_REGISTRYINDEX, timer->listener.m_Callback);
			lua_rawgeti(L, LUA_REGISTRYINDEX, timer->listener.m_Self);
			lua_pushvalue(L, -1);
			dmScript::SetInstance(L);
			if (!dmScript::IsInstanceValid(L)) {
				lua_pop(L, 2);
			} else {
				lua_pushinteger(L, timer->id);
				int ret = lua_pcall(L, 2, LUA_MULTRET, 0);
				if (ret != 0) {
					dmLogError("Error running timer callback: %s", lua_tostring(L, -1));
					lua_pop(L, 1);
				}
			}
			assert(top == lua_gettop(L));
		}
	}

	// Remove timers
	while(!g_TimersToRemove.Empty()) {
		int id = g_TimersToRemove.Back();
		g_TimersToRemove.Pop();
		Remove(id);
	}

	return dmExtension::RESULT_OK;
}

dmExtension::Result FinalizeTimerExtension(dmExtension::Params* params) {
	return dmExtension::RESULT_OK;
}


// Defold SDK uses a macro for setting up extension entry points:
//
// DM_DECLARE_EXTENSION(symbol, name, app_init, app_final, init, update, on_event, final)

DM_DECLARE_EXTENSION(Timer, LIB_NAME, AppInitializeTimerExtension, AppFinalizeTimerExtension, InitializeTimerExtension, UpdateTimerExtension, 0, FinalizeTimerExtension)
