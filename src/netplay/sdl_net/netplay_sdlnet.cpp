/*
 * Seven Kingdoms: Ancient Adversaries
 *
 * Copyright 1997,1998 Enlight Software Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

// Filename    : netplay_none.cpp
// Description : MultiPlayerSDL, SDL_net based multiplayer class
// Onwer       : Gilbert

#include <netplay.h>
#include <ALL.h>
#include <string.h>
#include <OVGALOCK.h>
#include <OBLOB.h>
#include <stdint.h>
#include <dbglog.h>
#include <SDL/SDL_net.h>

DBGLOG_DEFAULT_CHANNEL(NetPlay);

const uint32_t SYS_MSG_PID = 0xFF00FF00;
const Uint16 GAME_PORT = 1234;

enum {
	CMD_SESSION_INFO_REQUEST = 0xCD000001,
	CMD_SESSION_INFO_REPLY
};

SDLSessionDesc::SDLSessionDesc()
{
	id = 0;
	session_name[0] = '\0';
	pass_word[0] = '\0';
}

SDLSessionDesc::SDLSessionDesc(const SDLSessionDesc &src)
{
	id = src.id;
	strcpy(session_name, src.session_name);
	strcpy(pass_word, src.pass_word);
}

SDLSessionDesc& SDLSessionDesc::operator= (const SDLSessionDesc &src)
{
	id = src.id;
	strcpy(session_name, src.session_name);
	strcpy(pass_word, src.pass_word);
	return *this;
}

// to start a multiplayer game, first check if it is called from a
// lobbied (MultiPlayerSDL::is_lobbied)

// if it is a lobbied, call init_lobbied

// if not, call poll_service_provider; display them and let
// user to select, call init and pass the guid of the selected
// service; create_session or poll_sessions+join_session;

MultiPlayerSDL::MultiPlayerSDL() :
	current_sessions(sizeof(SDLSessionDesc), 10 )
{
	init_flag = 0;
	lobbied_flag = 0;
	supported_protocols = TCPIP;
	my_player_id = 0;
	host_flag = 0;
	allowing_connections = 0;
	recv_buffer = new char[MP_RECV_BUFFER_SIZE];
	recv_buffer_size = MP_RECV_BUFFER_SIZE;
	data_sock = NULL;
	listen_sock = NULL;
	sock_set = NULL;
}

MultiPlayerSDL::~MultiPlayerSDL()
{
	deinit();
	if (recv_buffer) {
		delete [] recv_buffer;
		recv_buffer = NULL;
	}
}

void MultiPlayerSDL::init(ProtocolType protocol_type)
{
	init_flag = 0;
	lobbied_flag = 0;
	my_player_id = 0;
	host_flag = 0;
	max_players = 0;

	if (!is_protocol_supported(protocol_type)) {
		ERR("[MultiPlayerSDL::init] trying to init unsupported protocol\n");
		return;
	}

	// TODO: add SDL initialization if required

	if (SDLNet_Init() == -1) {
		ERR("[MultiPlayerSDL::init] unable to init SDL_net: %s\n", SDLNet_GetError());
		return;
	}

	sock_set = SDLNet_AllocSocketSet(1);
	if (!sock_set) {
		ERR("[MultiPlayerSDL::init] SDLNet_AllocSocketSet: %s\n", SDLNet_GetError());
		SDLNet_Quit();
		return;
	}

	for (int i = 0; i < MAX_NATION; i++)
		player_pool[i].id = 0;

	init_flag = 1;
}

void MultiPlayerSDL::deinit()
{
	if (data_sock) {
		if (SDLNet_TCP_DelSocket(sock_set, data_sock) == -1)
			ERR("[MultiPlayerSDL::deinit] SDLNet_DelSocket: %s\n", SDLNet_GetError());
		SDLNet_TCP_Close(data_sock);
	}

	if (listen_sock)
		SDLNet_TCP_Close(listen_sock);

	SDLNet_FreeSocketSet(sock_set);
	SDLNet_Quit();

	init_flag = 0;
	lobbied_flag = 0;
	my_player_id = 0;
	host_flag = 0;
	allowing_connections = 0;
	data_sock = NULL;
	listen_sock = NULL;
	sock_set = NULL;
}

void MultiPlayerSDL::init_lobbied(int maxPlayers, char *cmdLine)
{
	ERR("[MultiPlayerSDL::init_lobbied] %d, %s\n", maxPlayers, cmdLine);
	if (cmdLine) {
		SDLSessionDesc session;

		strncpy(session.session_name, cmdLine, MP_SESSION_NAME_LEN-1);
		session.pass_word[0] = 0;

		current_sessions.linkin(&session);

		lobbied_flag = 2;
	} else {
		// hosting doesn't work yet
		lobbied_flag = 1;
	}
}

// return 0=not lobbied, 1=auto create, 2=auto join, 4=selectable
int MultiPlayerSDL::is_lobbied()
{
	return lobbied_flag;
}

// get_lobbied_name() is used to get the player's name when the game is
// launched to the lobby -- this probably won't ever be needed.
char *MultiPlayerSDL::get_lobbied_name()
{
	return NULL;
}

void MultiPlayerSDL::poll_supported_protocols()
{
}

bool MultiPlayerSDL::is_protocol_supported(ProtocolType protocol)
{
	return (protocol & supported_protocols) != 0;
}

int MultiPlayerSDL::poll_sessions()
{
	err_when(!init_flag);

	// poll_sessions should be called only by client
	if (host_flag) return FALSE;
	// for now, there is no need to search for additional sessions if we already got one
	if (current_sessions.size() > 0) return TRUE;

// skip the following code... it is a start for implementing a lobby game list, but it can't work right now
#if 0
	// establish connection with server
	if (SDLNet_ResolveHost(&ip_address, "localhost", GAME_PORT) == -1) {
		ERR("[MultiPlayerSDL::poll_sessions] failed to resolve hostname: %s\n", SDLNet_GetError());
		return FALSE;
	}

	data_sock = SDLNet_TCP_Open(&ip_address);
	if (!data_sock) {
		ERR("[MultiPlayerSDL::poll_sessions] failed to connect to server: %s\n", SDLNet_GetError());
		return FALSE;
	}

	int total = SDLNet_TCP_AddSocket(sock_set, data_sock);
	if (total == -1) {
		ERR("[MultiPlayerSDL::poll_sessions] SDLNet_AddSocket: %s\n", SDLNet_GetError());
	}

	// request session info from server

	send_session_info_request();

	// receive reply from server

	MSG("[MultiPlayerSDL::poll_sessions] waiting for reply ...\n");
	uint32_t tmp1, tmp2, tmp3;

	// skip unrelated messages, if any
	while (current_sessions.size() == 0) {
		// 'receive' will automatically process reply and add session to array
		receive(&tmp1, &tmp2, &tmp3);
	}

	MSG("[MultiPlayerSDL::poll_sessions] reply received\n");

	if (SDLNet_TCP_DelSocket(sock_set, data_sock) == -1)
		ERR("[MultiPlayerSDL::poll_sessions] SDLNet_DelSocket: %s\n", SDLNet_GetError());

	SDLNet_TCP_Close(data_sock);
	data_sock = NULL;
#endif

	return TRUE;
}

// return a session description
//
// <int> i			i-th session (i start from 1)
// return pointer to a session, NULL if no more
SDLSessionDesc *MultiPlayerSDL::get_session(int i)
{
	if( i <= 0 || i > current_sessions.size() )
		return NULL;
	return (SDLSessionDesc *)current_sessions.get(i);
}

// create a new session
//
// <char *> sessionName      arbitary name to identify a session, input from user
// <char *> playerName       name to identify the local player's name in this session
// <int>    maxPlayers       maximum no. of players in a session
//
// return TRUE if success
int MultiPlayerSDL::create_session(char *sessionName, char *playerName, int maxPlayers)
{
	IPaddress ip_address;

	err_when(!init_flag || maxPlayers <= 0 || maxPlayers > MAX_NATION);

	// open socket for listening

	if (SDLNet_ResolveHost(&ip_address, NULL, GAME_PORT) == -1) {
		ERR("[MultiPlayerSDL::create_session] failed to resolve hostname: %s\n", SDLNet_GetError());
		return FALSE;
	}

	listen_sock = SDLNet_TCP_Open(&ip_address);
	if (!listen_sock) {
		ERR("[MultiPlayerSDL::create_session] failed to start listening: %s\n", SDLNet_GetError());
		return FALSE;
	} else {
		MSG("[MultiPlayerSDL::create_session] waiting for clients on port: %d\n", (int)GAME_PORT);
	}

	joined_session.id = 789;
	char sname[] = "SDL_net test game";
	char spass[] = "p@ssw0rd";
	strcpy(joined_session.session_name, sname);
	strcpy(joined_session.pass_word, spass);

	host_flag = 1;
	max_players = maxPlayers;
	allowing_connections = 1;

	// Add hosts machine's player to the pool now
	my_player_id = create_player(playerName);
	err_when(!my_player_id);
	strncpy(my_name, playerName, MP_FRIENDLY_NAME_LEN);

	return TRUE;
}

// join a session, by passing the index passed into get_session()
// note : do not call poll_sessions between get_session and join_session
//
// <int> currentSessionIndex       the index passed into get_session()
//
// currentSessionIndex start from 1
int MultiPlayerSDL::join_session(int i, char *playerName)
{
	IPaddress ip_address;

	SDLSessionDesc *session = (SDLSessionDesc *)current_sessions.get(i);
	if (!session)
		return FALSE;

	// establish connection with server
	if (SDLNet_ResolveHost(&ip_address, session->session_name, GAME_PORT) == -1) {
		ERR("[MultiPlayerSDL::join_sessions] failed to resolve hostname: %s\n", SDLNet_GetError());
		return FALSE;
	}

	data_sock = SDLNet_TCP_Open(&ip_address);
	if (!data_sock) {
		ERR("[MultiPlayerSDL::join_sessions] failed to connect to server: %s\n", SDLNet_GetError());
		return FALSE;
	} else {
		MSG("[MultiPlayerSDL::join_sessions] successfully connected to server\n");
	}

	int total = SDLNet_TCP_AddSocket(sock_set, data_sock);
	if (total == -1) {
		ERR("[MultiPlayerSDL::join_session] SDLNet_AddSocket: %s\n", SDLNet_GetError());
	}

	// TODO: request information from host

	joined_session = *session;

	return TRUE;
}

void MultiPlayerSDL::close_session()
{
	// unused
}

void MultiPlayerSDL::disable_join_session()
{
	allowing_connections = 0;
}

void MultiPlayerSDL::accept_connections()
{
	TCPsocket connecting;

	// accept_connections shouldn't be used by clients
	if (!host_flag) return;

	connecting = SDLNet_TCP_Accept(listen_sock);
	if (!connecting) {
		return;
	}
	if (!allowing_connections) {
		SDLNet_TCP_Close(connecting);
		return;
	}

	MSG("[MultiPlayerSDL::accept_connections] client accepted\n");

	data_sock = connecting;

	int total = SDLNet_TCP_AddSocket(sock_set, data_sock);
	if (total == -1) {
		ERR("[MultiPlayerSDL::accept_connections] SDLNet_AddSocket: %s\n", SDLNet_GetError());
	}
}

// create_player(char *name)
//
// Creates a player to add to the pool
//
// <char *> name          name of the player
//
// This is only called by the host upon connection from a client. The host
// chooses the player's id.
//
// Returns the new player's id when there is room for a new player, and zero when
// there there is no room and the player was not added.
//
uint32_t MultiPlayerSDL::create_player(char *name)
{
	int i;

	// search for an empty slot
	for (i = 0; i < max_players; i++)
		if (!player_pool[i].id)
			break;
	if (i >= max_players)
		return 0;

	// add to the pool
	player_pool[i].id = i+1;
	strncpy(player_pool[i].name, name, MP_FRIENDLY_NAME_LEN);
	player_pool[i].connecting = 1;

	return player_pool[i].id;
}

void MultiPlayerSDL::poll_players()
{
	// TODO: player pool should be kept and sync'ed via server
	ERR("[MultiPlayerSDL::poll_players] unimplemented\n");
}

SDLPlayer *MultiPlayerSDL::get_player(int i)
{
	if (i < 1 || i > max_players || player_pool[i].id != i)
		return NULL;
	return &player_pool[i-1];
}

SDLPlayer *MultiPlayerSDL::search_player(uint32_t playerId)
{
	if (playerId < 1 || playerId > max_players || player_pool[playerId-1].id != playerId)
		return NULL;
	return &player_pool[playerId-1];
}

// determine whether a player is lost
//
// MultiPlayerSDL::received must be called (or remote.poll_msg) , 
// so if a player is really lost, the system message from 
// directPlay is received
//
int MultiPlayerSDL::is_player_connecting(uint32_t playerId)
{
	if (playerId < 1 || playerId > max_players || player_pool[playerId-1].id != playerId)
		return 0;
	return player_pool[playerId-1].connecting;
}

int MultiPlayerSDL::get_player_count()
{
	int count = 0;
	for (int i = 0; i < max_players; i++)
		if (player_pool[i].id == i+1 && player_pool[i].connecting)
			count++;
	return count;
}

// send udp message
//
// pass BROADCAST_PID as toId to all players
//
// return TRUE on success
//
int MultiPlayerSDL::send(uint32_t to, void * data, uint32_t msg_size)
{
	err_now("unimplemented");
	return FALSE;
}

// send tcp message
//
// pass BROADCAST_PID as toId to all players
//
// return TRUE on success
//
int MultiPlayerSDL::send_stream(uint32_t to, void * data, uint32_t msg_size)
{
	// TODO: send only if remote player joined the _session_

	if (!data_sock) {
		MSG("[MultiPlayerSDL::send_stream] no connection established\n");
		return FALSE;
	}

	int bytes_sent = 0;

	// message structure:
	//
	// uint32_t msg_size;  // message _data_ size
	// uint32_t to;        // receiver id
	// byte[]   data;      // byte array of size msg_size

	const int send_buf_size = sizeof(msg_size) + sizeof(to);
	char send_buf[send_buf_size];

	SDLNet_Write32(msg_size, send_buf);
	SDLNet_Write32(to, send_buf + sizeof(msg_size));

	bytes_sent += SDLNet_TCP_Send(data_sock, send_buf, send_buf_size);
	bytes_sent += SDLNet_TCP_Send(data_sock, data, msg_size);
	if (bytes_sent != send_buf_size + msg_size) {
		ERR("[MultiPlayerSDL::send_stream] error while sending data\n");
		return FALSE;
	}

	MSG("[MultiPlayerSDL::send_stream] bytes sent: %d to %d\n", (int)bytes_sent, (int)to);
	return TRUE;
}

// receive udp message
//
// return NULL if fails
// sysMsgCount records how many system messages have been handled
// notice : *sysMsgCount may be != 0, but return NULL
//
char *MultiPlayerSDL::receive(uint32_t * from, uint32_t * to, uint32_t * size, int *sysMsgCount)
{
	err_now("unimplemented");
	return FALSE;
}

// receive tcp message
//
// return NULL if fails
// sysMsgCount records how many system messages have been handled
// notice : *sysMsgCount may be != 0, but return NULL
//
char *MultiPlayerSDL::receive_stream(uint32_t * from, uint32_t * to, uint32_t * size, int *sysMsgCount)
{
	// have game host accept connections during game setup
	accept_connections();

	// TODO: rename sysMsgCount to playerLost and update the logic
	//       (because sysMsgCount is only used to determine playerLost event)
	if (sysMsgCount) *sysMsgCount = 0;

	uint32_t msg_size;
	uint32_t source_id;
	uint32_t target_id;

	char * data = receive_raw(&source_id, &target_id, &msg_size);

	if (target_id == SYS_MSG_PID)
	{
		process_sys_msg(msg_size, data);
		return NULL;
	}
	else
	{
		*from = source_id;
		*to = target_id;
		*size = msg_size;
		return data;
	}
}

char * MultiPlayerSDL::receive_raw(uint32_t * from, uint32_t * to, uint32_t * size)
{
	// used for logging
	static char client_str[] = "client";
	static char server_str[] = "server";
	char * remote_str = NULL;

	if (host_flag) remote_str = client_str;
	else remote_str = server_str;

	if (!data_sock) return NULL;

	int status = SDLNet_CheckSockets(sock_set, 1);
	if (status == -1) {
		ERR("[MultiPlayerSDL::receive] SDL_net error: %s\n", SDLNet_GetError());
		return NULL;
	} else if (status == 0) {
		return NULL; // no data to receive
	}

	uint32_t msg_size; // msg data size (without size itself and player id)
	uint32_t target_id;
	uint32_t bytes_received; // bytes already received
	int header_size = sizeof(msg_size) + sizeof(target_id);
	char * ptr = recv_buffer; // current buffer read pointer

	// try to receive message header

	status = SDLNet_TCP_Recv(data_sock, ptr, header_size);
	if (status <= 0)
	{
		// seems like remote machine has unexpectedly closed the connection
		MSG("[MultiPlayerSDL::receive] connection lost\n");

		if (SDLNet_TCP_DelSocket(sock_set, data_sock) == -1)
			ERR("[MultiPlayerSDL::receive] SDLNet_DelSocket: %s\n", SDLNet_GetError());
		SDLNet_TCP_Close(data_sock);
		data_sock = NULL;

		// TODO: check, if the player had joined the game _session_;
		//       in that case set player.connecting = 0

		return NULL;
	}
	else if (status != header_size)
	{
		ERR("[MultiPlayerSDL::receive] corrupted message header received\n");
		return NULL;
	}

	msg_size = SDLNet_Read32(ptr);
	ptr += sizeof(msg_size);
	target_id = SDLNet_Read32(ptr);
	ptr += sizeof(target_id);

	// receive message contents

	MSG("[MultiPlayerSDL::receive_raw] receiving message data, size: %d\n", (int)msg_size);

	bytes_received = 0;
	while (bytes_received < msg_size)
	{
		int count = SDLNet_TCP_Recv(data_sock, ptr, msg_size - bytes_received);

		if (count <= 0) {
			ERR("[MultiPlayerSDL::receive] corrupted message data from %s\n", remote_str);
			return NULL;
		}

		bytes_received += count;
		ptr += count;

		if (ptr > recv_buffer + MP_RECV_BUFFER_SIZE)
			ERR("[MultiPlayerSDL::receive] overflow while receiving data from %s\n", remote_str);
		// TODO: extend buffer to fit the message
	}

	MSG("[MultiPlayerSDL::receive] received: %d\n", bytes_received);

	*to = target_id;
	*size = msg_size;
	// TODO: use SDLNet_TCP_GetPeerAddress instead of hardcoding id values
	if (host_flag) *from = 654;
	else *from = 456;

	return recv_buffer + header_size;
}

void MultiPlayerSDL::process_sys_msg(uint32_t size, char * ptr)
{
	MSG("[MultiPlayerSDL::process_sys_msg] processing system msg\n");

	uint32_t command = SDLNet_Read32(ptr);
	ptr += sizeof(command);

	MSG("[MultiPlayerSDL::process_sys_msg] command: %d\n", (int)command);

	switch (command)
	{
		case CMD_SESSION_INFO_REQUEST:
			process_session_info_request();
			break;
		case CMD_SESSION_INFO_REPLY:
			process_session_info_reply(ptr);
			break;
		default:
			ERR("[MultiPlayerSDL::process_sys_msg] unknown command\n");
			break;
	}
}

void MultiPlayerSDL::send_session_info_request()
{
	// TODO: add additional checks

	uint32_t message_size = sizeof(uint32_t);
	uint32_t receiver_id  = SYS_MSG_PID;
	uint32_t command_id   = CMD_SESSION_INFO_REQUEST;

	const int send_buf_size = sizeof(message_size) +
	                          sizeof(receiver_id) +
	                          sizeof(command_id);

	char send_buf[send_buf_size];
	char * ptr = send_buf;

	SDLNet_Write32(message_size, ptr);
	ptr += sizeof(message_size);
	SDLNet_Write32(receiver_id, ptr);
	ptr += sizeof(receiver_id);
	SDLNet_Write32(command_id, ptr);

	int bytes_sent = SDLNet_TCP_Send(data_sock, send_buf, send_buf_size);
	if (bytes_sent != send_buf_size) {
		ERR("[MultiPlayerSDL::send_session_info_request] error while requesting session info: %s\n",
			SDLNet_GetError());
		//return FALSE;
	} else {
		MSG("[MultiPlayerSDL::send_session_info_request] query sent\n");
	}
}

void MultiPlayerSDL::process_session_info_request()
{
	send_session_info_reply();
}

void MultiPlayerSDL::send_session_info_reply()
{
	// TODO: add additional checks

	MSG("[MultiPlayerSDL::process_session_info_request]\n");

	uint32_t receiver_id  = SYS_MSG_PID;
	uint32_t command_id   = CMD_SESSION_INFO_REPLY;
	uint32_t message_size = 0;
	message_size += sizeof(command_id);
	message_size += sizeof(joined_session.id);
	message_size += MP_SESSION_NAME_LEN;
	message_size += MP_PASSWORD_LEN;

	const int send_buf_size = sizeof(message_size) +
	                          sizeof(receiver_id) +
	                          sizeof(command_id) +
	                          sizeof(joined_session.id) +
	                          MP_SESSION_NAME_LEN +
	                          MP_PASSWORD_LEN;

	char send_buf[send_buf_size];
	char * ptr = send_buf;

	SDLNet_Write32(message_size, ptr);
	ptr += sizeof(message_size);
	SDLNet_Write32(receiver_id, ptr);
	ptr += sizeof(receiver_id);
	SDLNet_Write32(command_id, ptr);
	ptr += sizeof(command_id);
	SDLNet_Write32(joined_session.id, ptr);
	ptr += sizeof(joined_session.id);
	memcpy(ptr, joined_session.session_name, MP_SESSION_NAME_LEN);
	ptr += MP_SESSION_NAME_LEN;
	memcpy(ptr, joined_session.pass_word, MP_PASSWORD_LEN);

	int bytes_sent = SDLNet_TCP_Send(data_sock, send_buf, send_buf_size);

	if (bytes_sent != send_buf_size) {
		ERR("[MultiPlayerSDL::process_session_info_request] error while sending session info: %s\n",
			SDLNet_GetError());
	} else {
		MSG("[MultiPlayerSDL::process_session_info_request] reply sent, size: %d\n", (int)bytes_sent);
	}
}

void MultiPlayerSDL::process_session_info_reply(char * ptr)
{
	SDLSessionDesc session;

	session.id = SDLNet_Read32(ptr);
	ptr += sizeof(session.id);
	memcpy(session.session_name, ptr, MP_SESSION_NAME_LEN);
	ptr += MP_SESSION_NAME_LEN;
	memcpy(session.pass_word, ptr, MP_PASSWORD_LEN);

	session.session_name[MP_SESSION_NAME_LEN] = '\0';
	session.pass_word[MP_PASSWORD_LEN] = '\0';

	current_sessions.linkin(&session);
}

/*
static int sort_session_id(const void *a, const void *b)
{
	return memcmp( &((SDLSessionDesc *)a)->guidInstance, &((SDLSessionDesc *)b)->guidInstance,
		sizeof(GUID) );
}
*/

static int sort_session_name(const void *a, const void *b)
{
	return strcmp( ((SDLSessionDesc *)a)->name_str(), ((SDLSessionDesc *)b)->name_str() );
}

// sort current_sessions
// <int> sortType, 1=sort by GUID, 2=sort by session name
void MultiPlayerSDL::sort_sessions(int sortType )
{
	ERR("[MultiPlayerSDL::sort_sessions] calling partially implemented method\n");

	// BUGHERE : quick_sort is a DynArray function but current_sessions is DynArrayB
	switch(sortType)
	{
	case 1:
		//current_sessions.quick_sort(sort_session_id);
		break;
	case 2:
		current_sessions.quick_sort(sort_session_name);
		break;
	default:
		err_here();
	}
}

