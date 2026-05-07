#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <unordered_map>
#include <Windows.h>
#include <chrono>
using namespace std;

#include "..\..\SERVER\SERVER\protocol_2026.h"

sf::TcpSocket s_socket;

constexpr auto SCREEN_WIDTH = 16;
constexpr auto SCREEN_HEIGHT = 16;

constexpr auto TILE_WIDTH = 65;
constexpr auto WINDOW_WIDTH = SCREEN_WIDTH * TILE_WIDTH;
constexpr auto WINDOW_HEIGHT = SCREEN_WIDTH * TILE_WIDTH;

int g_left_x;
int g_top_y;
int g_myid;
int g_my_hp;
int g_my_max_hp;

sf::RenderWindow* g_window;
sf::Font g_font;

// NPC ID를 내부 인덱스로 변환
int from_protocol_id(int protocol_id)
{
	if (protocol_id < MAX_PLAYERS) {
		return protocol_id;
	}
	else {
		return NPC_ID_START - 1000000 + protocol_id;
	}
}

class OBJECT {
private:
	bool m_showing;
	sf::Sprite m_sprite;

	sf::Text m_name;
	sf::Text m_chat;
	chrono::system_clock::time_point m_mess_end_time;
public:
	int id;
	int m_x, m_y;
	int m_hp, m_max_hp;
	unsigned long long m_exp;
	unsigned char m_level;
	char name[MAX_NAME_LEN];
	OBJECT(sf::Texture& t, int x, int y, int x2, int y2) {
		m_showing = false;
		m_sprite.setTexture(t);
		m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));
		set_name("NONAME");
		m_mess_end_time = chrono::system_clock::now();
		m_hp = 0;
		m_max_hp = 0;
		m_exp = 0;
		m_level = 0;
	}
	OBJECT() {
		m_showing = false;
		m_hp = 0;
		m_max_hp = 0;
		m_exp = 0;
		m_level = 0;
	}
	void show()
	{
		m_showing = true;		
	}
	void hide()
	{
		m_showing = false;
	}

	void a_move(int x, int y) {
		m_sprite.setPosition((float)x, (float)y);
	}

	void a_draw() {
		g_window->draw(m_sprite);
	}

	void move(int x, int y) {
		m_x = x;
		m_y = y;
	}

	void set_hp(int hp, int max_hp) {
		m_hp = hp;
		m_max_hp = max_hp;
	}

	void set_stats(unsigned long long exp, unsigned char level) {
		m_exp = exp;
		m_level = level;
	}

	void draw() {
		if (false == m_showing) return;
		float rx = (m_x - g_left_x) * 65.0f + 1;
		float ry = (m_y - g_top_y) * 65.0f + 1;
		m_sprite.setPosition(rx, ry);
		g_window->draw(m_sprite);
		auto size = m_name.getGlobalBounds();
		if (m_mess_end_time < chrono::system_clock::now()) {
			m_name.setPosition(rx + 32 - size.width / 2, ry - 10);
			g_window->draw(m_name);
		}
		else {
			m_chat.setPosition(rx + 32 - size.width / 2, ry - 10);
			g_window->draw(m_chat);
		}
	}
	void set_name(const char str[]) {
		m_name.setFont(g_font);
		m_name.setString(str);
		if (id < NPC_ID_START) m_name.setFillColor(sf::Color(255, 255, 255));
		else m_name.setFillColor(sf::Color(255, 255, 0));
		m_name.setStyle(sf::Text::Bold);
	}

	void set_chat(const char str[]) {
		m_chat.setFont(g_font);
		m_chat.setString(str);
		m_chat.setFillColor(sf::Color(255, 255, 255));
		m_chat.setStyle(sf::Text::Bold);
		m_mess_end_time = chrono::system_clock::now() + chrono::seconds(3);
	}
};

OBJECT avatar;
unordered_map <int, OBJECT> players;

OBJECT white_tile;
OBJECT black_tile;

sf::Texture* board;
sf::Texture* pieces;

void client_initialize()
{
	board = new sf::Texture;
	pieces = new sf::Texture;
	board->loadFromFile("chessmap.bmp");
	pieces->loadFromFile("chess2.png");
	if (false == g_font.loadFromFile("cour.ttf")) {
		cout << "Font Loading Error!\n";
		exit(-1);
	}
	white_tile = OBJECT{ *board, 5, 5, TILE_WIDTH, TILE_WIDTH };
	black_tile = OBJECT{ *board, 69, 5, TILE_WIDTH, TILE_WIDTH };
	avatar = OBJECT{ *pieces, 128, 0, 64, 64 };
	avatar.move(4, 4);
}

void client_finish()
{
	players.clear();
	delete board;
	delete pieces;
}

void ProcessPacket(char* ptr)		
{
	unsigned char packet_type = ptr[1];
	
	switch (packet_type)
	{
	case S2C_LOGIN_RESULT:
	{
		S2C_LoginResult* packet = reinterpret_cast<S2C_LoginResult*>(ptr);
		if (packet->success) {
			cout << "Login successful: " << packet->message << endl;
		}
		else {
			cout << "Login failed: " << packet->message << endl;
		}
		break;
	}

	case S2C_AVATAR_INFO:
	{
		S2C_AvatarInfo* packet = reinterpret_cast<S2C_AvatarInfo*>(ptr);
		g_myid = packet->playerId;
		avatar.id = g_myid;
		avatar.move(packet->x, packet->y);
		avatar.set_hp(packet->hp, packet->max_hp);
		avatar.set_stats(packet->exp, packet->level);
		g_left_x = packet->x - SCREEN_WIDTH / 2;
		g_top_y = packet->y - SCREEN_HEIGHT / 2;
		avatar.show();
		g_my_hp = packet->hp;
		g_my_max_hp = packet->max_hp;
		cout << "Avatar initialized at (" << packet->x << ", " << packet->y << ")" << endl;
		break;
	}

	case S2C_ADD_OBJECT:
	{
		S2C_AddObject* my_packet = reinterpret_cast<S2C_AddObject*>(ptr);
		int protocol_id = my_packet->object_id;

		if (protocol_id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			avatar.set_hp(my_packet->hp, my_packet->max_hp);
			avatar.set_stats(my_packet->exp, my_packet->level);
			g_left_x = my_packet->x - SCREEN_WIDTH / 2;
			g_top_y = my_packet->y - SCREEN_HEIGHT / 2;
			avatar.show();
		}
		else if (protocol_id < NPC_ID_START) {
			players[protocol_id] = OBJECT{ *pieces, 0, 0, 64, 64 };
			players[protocol_id].id = protocol_id;
			players[protocol_id].move(my_packet->x, my_packet->y);
			players[protocol_id].set_hp(my_packet->hp, my_packet->max_hp);
			players[protocol_id].set_stats(my_packet->exp, my_packet->level);
			players[protocol_id].set_name(my_packet->obj_name);
			players[protocol_id].show();
			cout << "Player added: " << my_packet->obj_name << " at (" << my_packet->x << ", " << my_packet->y << ")" << endl;
		}
		else {
			players[protocol_id] = OBJECT{ *pieces, 256, 0, 64, 64 };
			players[protocol_id].id = protocol_id;
			players[protocol_id].move(my_packet->x, my_packet->y);
			players[protocol_id].set_hp(my_packet->hp, my_packet->max_hp);
			players[protocol_id].set_stats(my_packet->exp, my_packet->level);
			players[protocol_id].set_name(my_packet->obj_name);
			players[protocol_id].show();
			cout << "NPC added: " << my_packet->obj_name << " at (" << my_packet->x << ", " << my_packet->y << ")" << endl;
		}
		break;
	}

	case S2C_MOVE_OBJECT:
	{
		S2C_MoveObject* my_packet = reinterpret_cast<S2C_MoveObject*>(ptr);
		int protocol_id = my_packet->object_id;
		
		if (protocol_id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - SCREEN_WIDTH / 2;
			g_top_y = my_packet->y - SCREEN_HEIGHT / 2;
		}
		else {
			if (players.find(protocol_id) != players.end()) {
				players[protocol_id].move(my_packet->x, my_packet->y);
			}
		}
		break;
	}

	case S2C_REMOVE_OBJECT:
	{
		S2C_RemoveObject* my_packet = reinterpret_cast<S2C_RemoveObject*>(ptr);
		int protocol_id = my_packet->object_id;
		
		if (protocol_id == g_myid) {
			avatar.hide();
		}
		else {
			auto it = players.find(protocol_id);
			if (it != players.end()) {
				cout << "Removing object: " << protocol_id << endl;
				players.erase(it);
			}
		}
		break;
	}

	case S2C_CHAT_MESSAGE:
	{
		S2C_ChatMessage* my_packet = reinterpret_cast<S2C_ChatMessage*>(ptr);
		int protocol_id = my_packet->object_id;
		
		if (protocol_id == g_myid) {
			avatar.set_chat(my_packet->message);
		}
		else {
			if (players.find(protocol_id) != players.end()) {
				players[protocol_id].set_chat(my_packet->message);
			}
		}
		break;
	}

	case S2C_STATUS_CHANGE:
	{
		S2C_StatusChange* my_packet = reinterpret_cast<S2C_StatusChange*>(ptr);
		int protocol_id = my_packet->object_id;
		
		if (protocol_id == g_myid) {
			avatar.set_hp(my_packet->hp, my_packet->max_hp);
			avatar.set_stats(my_packet->exp, my_packet->level);
			g_my_hp = my_packet->hp;
			g_my_max_hp = my_packet->max_hp;
		}
		else {
			if (players.find(protocol_id) != players.end()) {
				players[protocol_id].set_hp(my_packet->hp, my_packet->max_hp);
				players[protocol_id].set_stats(my_packet->exp, my_packet->level);
			}
		}
		break;
	}

	default:
		printf("Unknown PACKET type [%d]\n", packet_type);
	}
}

void process_data(char* net_buf, size_t io_byte)
{
	char* ptr = net_buf;
	static size_t in_packet_size = 0;
	static size_t saved_packet_size = 0;
	static char packet_buffer[4096];

	while (0 != io_byte) {
		if (0 == in_packet_size) in_packet_size = ptr[0];
		if (io_byte + saved_packet_size >= in_packet_size) {
			memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
			ProcessPacket(packet_buffer);
			ptr += in_packet_size - saved_packet_size;
			io_byte -= in_packet_size - saved_packet_size;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else {
			memcpy(packet_buffer + saved_packet_size, ptr, io_byte);
			saved_packet_size += io_byte;
			io_byte = 0;
		}
	}
}

void client_main()
{
	char net_buf[4096];
	size_t	received;

	auto recv_result = s_socket.receive(net_buf, 4096, received);
	if (recv_result == sf::Socket::Error)
	{
		wcout << L"Recv 에러!" << endl;
		exit(-1);
	}
	if (recv_result == sf::Socket::Disconnected) {
		wcout << L"Disconnected\n" << endl;
		exit(-1);
	}
	if (recv_result != sf::Socket::NotReady)
		if (received > 0) process_data(net_buf, received);

	for (int i = 0; i < SCREEN_WIDTH; ++i)
		for (int j = 0; j < SCREEN_HEIGHT; ++j)
		{
			int tile_x = i + g_left_x;
			int tile_y = j + g_top_y;
			if ((tile_x < 0) || (tile_y < 0)) continue;
			if (0 == (tile_x / 3 + tile_y / 3) % 2) {
				white_tile.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
				white_tile.a_draw();
			}
			else
			{
				black_tile.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
				black_tile.a_draw();
			}
		}
	avatar.draw();
	for (auto& pl : players) pl.second.draw();
	sf::Text text;
	text.setFont(g_font);
	char buf[100];
	sprintf_s(buf, "(%d, %d) HP:%d/%d Lvl:%d Players:%zu", avatar.m_x, avatar.m_y, g_my_hp, g_my_max_hp, avatar.m_level, players.size());
	text.setString(buf);
	g_window->draw(text);
}

void send_packet(void *packet)
{
	unsigned char *p = reinterpret_cast<unsigned char *>(packet);
	size_t sent = 0;
	sf::Socket::Status status = s_socket.send(packet, p[0], sent);
	if (status == sf::Socket::Error) {
		wcout << L"Send Error!" << endl;
	}
}

int main()
{
	wcout.imbue(locale("korean"));
	
	wcout << L"서버에 연결 시도 중... (포트: " << PORT << L")" << endl;
	sf::Socket::Status status = s_socket.connect("127.0.0.1", PORT);

	if (status != sf::Socket::Done) {
		wcout << L"서버와 연결할 수 없습니다." << endl;
		exit(-1);
	}

	wcout << L"서버 연결 성공!" << endl;
	s_socket.setBlocking(false);

	client_initialize();
	
	C2S_Login p;
	p.size = sizeof(C2S_Login);
	p.type = C2S_LOGIN;

	string player_name{ "P" };
	player_name += to_string(GetCurrentProcessId());
	
	strcpy_s(p.username, MAX_NAME_LEN, player_name.c_str());
	send_packet(&p);
	avatar.set_name(p.username);
	
	wcout << L"로그인 패킷 전송: " << player_name.c_str() << endl;

	sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "2D CLIENT");
	g_window = &window;

	while (window.isOpen())
	{
		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				window.close();
			if (event.type == sf::Event::KeyPressed) {
				switch (event.key.code) {
				case sf::Keyboard::Left:
				{
					C2S_Move move_packet;
					move_packet.size = sizeof(C2S_Move);
					move_packet.type = C2S_MOVE;
					move_packet.x = avatar.m_x - 1;
					move_packet.y = avatar.m_y;
					move_packet.move_time = 100;
					send_packet(&move_packet);
					break;
				}
				case sf::Keyboard::Right:
				{
					C2S_Move move_packet;
					move_packet.size = sizeof(C2S_Move);
					move_packet.type = C2S_MOVE;
					move_packet.x = avatar.m_x + 1;
					move_packet.y = avatar.m_y;
					move_packet.move_time = 100;
					send_packet(&move_packet);
					break;
				}
				case sf::Keyboard::Up:
				{
					C2S_Move move_packet;
					move_packet.size = sizeof(C2S_Move);
					move_packet.type = C2S_MOVE;
					move_packet.x = avatar.m_x;
					move_packet.y = avatar.m_y - 1;
					move_packet.move_time = 100;
					send_packet(&move_packet);
					break;
				}
				case sf::Keyboard::Down:
				{
					C2S_Move move_packet;
					move_packet.size = sizeof(C2S_Move);
					move_packet.type = C2S_MOVE;
					move_packet.x = avatar.m_x;
					move_packet.y = avatar.m_y + 1;
					move_packet.move_time = 100;
					send_packet(&move_packet);
					break;
				}
				case sf::Keyboard::Escape:
				{
					C2S_Logout logout_packet;
					logout_packet.size = sizeof(C2S_Logout);
					logout_packet.type = C2S_LOGOUT;
					send_packet(&logout_packet);
					window.close();
					break;
				}
				default:
					break;
				}
			}
		}

		window.clear();
		client_main();
		window.display();
	}
	client_finish();

	return 0;
}