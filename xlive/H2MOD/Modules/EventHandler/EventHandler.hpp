
#include "Blam/Common/Common.h"
#include "H2MOD/Modules/ServerConsole/ServerConsole.h"

enum EventType
{
	player_leave,
	player_join,
	gamestate_change,
	game_loop,
	server_command,
	map_load,
	countdown_start
};
enum EventExecutionType
{
	before,
	after
};

using CountdownStartEvent = void(*)();
using GameStateEvent = void(*)(game_life_cycle state);

template<typename T> class EventCallback
{
public:
	T callback;
	EventType type;
	EventExecutionType execution_type;
	bool threaded;
	bool runOnce;
	bool hasRun = false;
	EventCallback(const T callback, EventType type, EventExecutionType execution_type = after, bool threaded = false, bool runOnce = false)
	{
		this->callback = callback;
		this->type = type;
		this->execution_type = execution_type;
		this->threaded = threaded;
		this->runOnce = runOnce;
	}
};
extern std::vector<EventCallback<void*>> events;

struct GameStateCallback
{
	std::string name;
	game_life_cycle  gameState;
	std::function<void()> callback;
	bool runOnce;
	bool hasRun = false;
	/**
	 * \brief Callback that executes when the GameState reaches the given GameState
	 * \param name A name that is stored with this callback that can later be referenced for removal.
	 * \param gameState The GameState type in which this should execute
	 * \param callback a void that will be executed when this event happens.
	 */
	GameStateCallback(std::string name, game_life_cycle  gameState, std::function<void()> callback, bool runOnce = false)
	{
		this->name = name;
		this->gameState = gameState;
		this->callback = callback;
		this->runOnce = runOnce;
	}
};
struct NetworkPeerEventCallback
{
	
	std::string name;
	std::function<void(int)> callback;
	/**
	 * \brief Basic callback for when a peer action happens in the network session.
	 * \param name A name that is stored with this callback that can later be referenced for removal.
	 * \param callback a void that will be executed when this event happens the paramter in is the peer index.
	 */
	NetworkPeerEventCallback(std::string name, std::function<void(int)> callback){
		this->name = name;
		this->callback = callback;
	}
};
struct GameLoopEventCallback
{
	std::string name;
	std::function<void()> callback;
	GameLoopEventCallback(std::string name, std::function<void()> callback)
	{
		this->name = name;
		this->callback = callback;
	}
};
struct PlayerControlChangeEventCallback
{
	std::string name;
	std::function<void(float, float)> callback;
	PlayerControlChangeEventCallback(std::string name, std::function<void(float, float)> callback)
	{
		this->name = name;
		this->callback = callback;
	}
};
struct MapLoadEventCallback
{
	std::string name;
	e_engine_type Type;
	std::function<void()> callback;
	MapLoadEventCallback(std::string name, e_engine_type type, std::function<void()> callback)
	{
		this->name = name;
		this->Type = type;
		this->callback = callback;
	}
};
struct ServerCommandEventCallback
{
	std::string name;
	std::function<void()> callback;
	ServerConsole::ServerConsoleCommands command;
	ServerCommandEventCallback(std::string name, std::function<void()> callback, ServerConsole::ServerConsoleCommands command)
	{
		this->name = name;
		this->callback = callback;
		this->command = command;
	}
};
struct CountdownStartedEventCallback
{
	std::string name;
	std::function<void()> callback;
	CountdownStartedEventCallback(std::string name, std::function<void()> callback)
	{
		this->name = name;
		this->callback = callback;
	}
};

namespace EventHandler
{
	/**
	 * \brief Adds a callback to be executed when the GameState changes
	 * \param callback A GameStateCallback to be executed when the event happens.
	 * \param threaded Tells the EventHandler whether or not this callback should be ran in its own thread with other threaded callbacks.
	 */
	void registerGameStateCallback(GameStateCallback callback, bool threaded);
	/**
	 * \brief Removes a callback from the store
	 * \param name Name of the callback to remove.
	 */
	void removeGameStateCallback(std::string name);
	/**
	 * \brief Executes the callbacks that match the given GameState.
	 * \param gamestate
	 */
	void executeGameStateCallbacks(BYTE gamestate);

	void cleanupGameStateCallbacks(BYTE gamestate);

	/**
	 * \brief Adds a callback to be executed when the NetworkSession recieves a player_add packet
	 * \param callback A NetworkPeerEventCallback to be executed when the event happens.
	 * \param threaded Tells the EventHandler whether or not this callback should be ran in its own thread with other threaded callbacks.
	 */
	void registerNetworkPlayerAddCallback(NetworkPeerEventCallback callback, bool threaded);
	/**
	 * \brief Removes a callback from the store
	 * \param name Name of the callback to remove
	 */
	void removeNetworkPlayerAddCallback(std::string name);
	/**
	 * \brief Executes the callbacks passing the Peer Index to the function
	 * \param peerIndex 
	 */
	void executeNetworkPlayerAddCallbacks(int peerIndex);

	/**
	 * \brief Adds a callback to be executed when the NetworkSession recieves a player_remove packet
	 * \param callback A NetworkPeerEventCallback to be executed when the event happens.
	 * \param threaded Tells the EventHandler whether or not this callback should be ran in its own thread with other threaded callbacks.
	 */
	void registerNetworkPlayerRemoveCallback(NetworkPeerEventCallback callback, bool threaded);
	/**
	 * \brief Removes a callback from the store
	 * \param name Name of the callback to remove
	 */
	void removeNetworkPlayerRemoveCallback(std::string name);
	/**
	 * \brief Executes the callbacks passing the Peer Index to the function
	 * \param peerIndex 
	 */
	void executeNetworkPlayerRemoveCallbacks(int peerIndex);


	/**
	 * \brief Adds a callback to be executed everytime main_game_loop is ran
	 * \param callback A GameLoopEventCallback to be executed when the event happens.
	 * \param threaded Tells the EventHandler whether or not this callback should be ran in its own thread with other threaded callbacks.
	 */
	void registerGameLoopCallback(GameLoopEventCallback callback, bool threaded);

	/**
	 * \brief Removes a callback from the store
	 * \param name Name of the callback to remove
	 */
	void removeGameLoopCallback(std::string name);
	/**
	 * \brief Executes the callbacks with no information passed to the function
	 */
	void executeGameLoopCallbacks();

	/**
	 * \brief Adds a callback to be executed when a server command is recieved
	 * \param callback A ServerCommandEventCallback to be executed when the event happens, give that the server command being executed matches the command inside the Callback object
	 * \param threaded Tells the EventHandler whether or not this callback should be ran in its own thread with other threaded callbacks.
	 */
	void registerServerCommandCallback(ServerCommandEventCallback callback, bool threaded);
	/**
	 * \brief Remvoes a callback from the store
	 * \param name Name of the callback to remove
	 */
	void removeServerCommandCallback(std::string name);
	/**
	 * \brief Executes the callbacks that match the input ServerConsoleCommands value with no informations passed to the function 
	 * \param command The server command that has been called
	 */
	void executeServerCommandCallback(ServerConsole::ServerConsoleCommands command);

	void registerPlayerControlCallback(PlayerControlChangeEventCallback callback, bool threaded);

	void removePlayerControlCallback(std::string name);

	void executePlayerControlCallback(float yaw, float pitch);

	//void registerMapLoadCallback(MapLoadEventCallback callback, bool threaded);
	//void removeMapLoadCallback(std::string name);
	//void executeMapLoadCallback(e_engine_type engine_type);

	//void registerCountdownStartCallback(const EventFunction callback, std::string name, bool threaded, bool runOnce = false);
	//void removeCountdownStartCallback(std::string name);
	//void executeCountdownStartCallback();

	//void register_callback(const EventFunction callback, EventType type, EventExecutionType execution_type, bool forward_args = false, bool threaded = false, bool run_once = false);

	//namespace
	//{
		
	//}

	template<typename T> static inline void remove_callback(const T callback, EventType type, EventExecutionType execution_type)
	{
		for (std::size_t i = 0; i < events.size(); i++) {
			auto event = events[i];
			if ((T)event.callback == callback && event.type == type && event.execution_type == execution_type) {
				events.erase(events.begin() + i);
				return;
			}
		}
	}
	template<typename T> static inline void cleanup_callbacks(EventType type, EventExecutionType execution_type)
	{
		auto it = events.begin();
		while(it != events.end())
		{
			if (it->runOnce && it->hasRun && it->type == type && it->execution_type == execution_type)
				it = events.erase(it);
			else
				++it;
		}
	}
	template<typename T> static inline void register_callback(const T callback, EventType type, EventExecutionType execution_type = after, bool threaded = false, bool run_once = false)
	{
		//Prevent duplicate events
		//remove_callback<T>(callback, type, execution_type);
		//auto events = (std::vector<EventCallback<T>>)events;
		events.emplace_back(callback, type, execution_type, threaded, run_once);
		LOG_INFO_GAME("{}", events.size());
	}
	template<typename T, typename ... Args>
	static void execute_callback(EventType type, EventExecutionType execution_type, Args&& ... args)
	{
		for (std::vector<EventCallback<void*>>::iterator it = events.begin(); it != events.end(); ++it) {
			if (it->type == type && it->execution_type == execution_type/* && event.threaded == threaded*/)
			{
				((T)it->callback)(std::forward<Args>(args) ...);
				it->hasRun = true;
			}
		}
	}
}
