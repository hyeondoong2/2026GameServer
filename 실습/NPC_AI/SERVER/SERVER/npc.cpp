#include <iostream>
#include <array>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <chrono>
#include <atomic>
#include <cstring>
#include <concurrent_priority_queue.h>
#include <tbb/concurrent_unordered_map.h>
#include "protocol_2026.h"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")
using namespace std;
using namespace std::chrono;

constexpr int VIEW_RANGE = 5;
constexpr int MOVE_COOL_TIME = 1000; // ms
constexpr int BUF_SIZE = 1024;
constexpr int NPC_FIRST_INDEX = MAX_PLAYERS;
constexpr int NPC_END_INDEX = MAX_PLAYERS + NUM_NPCS;
constexpr int DEFAULT_VISUAL_ID = 0;
constexpr int DEFAULT_MAX_HP = 100;
constexpr unsigned char DEFAULT_LEVEL = 1;

constexpr int SECTOR_SIZE = VIEW_RANGE + 5;
constexpr int SECTOR_ROWS = WORLD_HEIGHT / SECTOR_SIZE + 1;
constexpr int SECTOR_COLS = WORLD_WIDTH / SECTOR_SIZE + 1;

std::atomic<int> player_index = 1;
std::atomic<int> current_user_count = 0;

struct SECTOR
{
    std::mutex m_mutex;
    std::unordered_set<int> players;
};

SECTOR g_sectors[SECTOR_ROWS][SECTOR_COLS];

constexpr int EVENT_MOVE = 1;

constexpr int to_protocol_id(int object_index)
{
    return object_index < MAX_PLAYERS
        ? object_index
        : NPC_ID_START + (object_index - NPC_FIRST_INDEX);
}

short clamp_world_x(short value)
{
    if (value < 0) return 0;
    if (value >= WORLD_WIDTH) return WORLD_WIDTH - 1;
    return value;
}

short clamp_world_y(short value)
{
    if (value < 0) return 0;
    if (value >= WORLD_HEIGHT) return WORLD_HEIGHT - 1;
    return value;
}

template <size_t Size>
void copy_fixed_string(char(&dest)[Size], const char* src)
{
    memcpy(dest, src, Size);
    dest[Size - 1] = '\0';
}

struct event_type
{
    int obj_id;
    system_clock::time_point wakeup_time;
    int event_id;
    int target_id;

    constexpr bool operator < (const event_type& _Left) const
    {
        return (wakeup_time > _Left.wakeup_time);
    }
};

// 우선순위 큐를 사용하여 타이머 이벤트를 관리. 
// 가장 빠른 시간에 깨워야 할 이벤트가 큐의 맨 앞에 오도록 함.
concurrency::concurrent_priority_queue<event_type> timer_queue;


enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND, OP_NPCMOVE };
class OVER_EXP
{
public:
    WSAOVERLAPPED _over;
    WSABUF _wsabuf;
    char _send_buf[BUF_SIZE];
    COMP_TYPE _comp_type;
    int _ai_target_obj;
    SOCKET _socket;

    OVER_EXP()
    {
        _wsabuf.len = BUF_SIZE;
        _wsabuf.buf = _send_buf;
        _comp_type = OP_RECV;
        ZeroMemory(&_over, sizeof(_over));
    }
    OVER_EXP(const void* packet)
    {
        auto raw_packet = reinterpret_cast<const unsigned char*>(packet);
        _wsabuf.len = raw_packet[0];
        _wsabuf.buf = _send_buf;
        ZeroMemory(&_over, sizeof(_over));
        _comp_type = OP_SEND;
        memcpy(_send_buf, packet, raw_packet[0]);
    }
};

enum S_STATE { ST_FREE, ST_ALLOC, ST_INGAME };
class SESSION
{
    OVER_EXP _recv_over;

public:
    mutex _s_lock;
    S_STATE _state;
    int _id;
    SOCKET _socket;
    short	_x, _y;
    char	_name[MAX_NAME_LEN];
    int		_prev_remain;
    unordered_set <int> _view_list;
    mutex	_vl;
    int last_move_time;
    int visual_id;
    int hp;
    int max_hp;
    unsigned long long exp;
    unsigned char level;
    std::atomic<bool> _active_npc;	// 여러 스레드가 동시에 읽고 쓸 수 있으므로 atomic으로 선언.
    system_clock::time_point npc_last_move_time;

    // view list
    std::unordered_set<int> m_visible_players;
    std::mutex m_visible_mutex;

    // sector
    int m_sector_x;
    int m_sector_y;

public:
    // NPC용 생성자
    SESSION(int id) : _socket(0), _id(id)
    {
        _x = rand() % WORLD_WIDTH;
        _y = rand() % WORLD_HEIGHT;
        sprintf_s(_name, MAX_NAME_LEN, "NPC%d", to_protocol_id(id));
        _state = ST_INGAME; // NPC는 태어나자마자 바로 게임 월드에 존재함
        _prev_remain = 0;
        last_move_time = 0;
        visual_id = DEFAULT_VISUAL_ID;
        hp = DEFAULT_MAX_HP;
        max_hp = DEFAULT_MAX_HP;
        exp = 0;
        level = DEFAULT_LEVEL;
        _active_npc = false;
        npc_last_move_time = system_clock::now();

        m_sector_x = _x / SECTOR_SIZE;
        m_sector_y = _y / SECTOR_SIZE;
    }

    // 플레이어용 생성자
    SESSION(SOCKET s, int id) : _socket(s), _id(id)
    {
        _x = rand() % WORLD_WIDTH;
        _y = rand() % WORLD_HEIGHT;
        _name[0] = 0;
        _state = ST_ALLOC;
        _prev_remain = 0;
        last_move_time = 0;
        visual_id = DEFAULT_VISUAL_ID;
        hp = DEFAULT_MAX_HP;
        max_hp = DEFAULT_MAX_HP;
        exp = 0;
        level = DEFAULT_LEVEL;
        _active_npc = false;

        // 좌표 정해진 후 섹터 계산
        m_sector_x = _x / SECTOR_SIZE;
        m_sector_y = _y / SECTOR_SIZE;
    }

    ~SESSION()
    {
        if (_socket != 0)
        {
            closesocket(_socket);
        }
    }

    bool is_visible(short x, short y)
    {
        return abs(_x - x) <= VIEW_RANGE
            && abs(_y - y) <= VIEW_RANGE;
    }

    void do_recv()
    {
        DWORD recv_flag = 0;
        memset(&_recv_over._over, 0, sizeof(_recv_over._over));
        _recv_over._wsabuf.len = BUF_SIZE - _prev_remain;
        _recv_over._wsabuf.buf = _recv_over._send_buf + _prev_remain;
        WSARecv(_socket, &_recv_over._wsabuf, 1, 0, &recv_flag,
            &_recv_over._over, 0);
    }

    void do_send(const void* packet)
    {
        OVER_EXP* sdata = new OVER_EXP{ packet };
        WSASend(_socket, &sdata->_wsabuf, 1, 0, 0, &sdata->_over, 0);
    }
    void send_login_info_packet()
    {
        S2C_LoginResult result{};
        result.size = sizeof(S2C_LoginResult);
        result.type = S2C_LOGIN_RESULT;
        result.success = true;
        strcpy_s(result.message, "Login success");
        do_send(&result);

        S2C_AvatarInfo info{};
        info.size = sizeof(S2C_AvatarInfo);
        info.type = S2C_AVATAR_INFO;
        info.playerId = _id;
        info.visualId = visual_id;
        info.x = _x;
        info.y = _y;
        info.hp = hp;
        info.max_hp = max_hp;
        info.exp = exp;
        info.level = level;
        do_send(&info);
    }
    void send_move_packet(int c_id);
    void send_add_player_packet(int c_id);
    void send_chat_packet(int c_id, const char* mess);
    void send_remove_player_packet(int c_id)
    {
        _vl.lock();
        if (_view_list.count(c_id))
            _view_list.erase(c_id);
        else
        {
            _vl.unlock();
            return;
        }
        _vl.unlock();
        S2C_RemoveObject p{};
        p.object_id = c_id;
        p.size = sizeof(S2C_RemoveObject);
        p.type = S2C_REMOVE_OBJECT;
        do_send(&p);
    }
    void do_random_move();
    void heart_beat()
    {
        // Move active NPCs and notify nearby players.
        do_random_move();
    }

    void wake_up()
    {
        // Register one timer event per inactive NPC.
        bool expected = false;
        if (false == _active_npc.compare_exchange_strong(expected, true))
            return;

        event_type ev;
        ev.obj_id = _id;
        ev.event_id = EVENT_MOVE;
        ev.target_id = -1;
        ev.wakeup_time = system_clock::now() + milliseconds(MOVE_COOL_TIME);
        timer_queue.push(ev);
    }
};

HANDLE h_iocp;

tbb::concurrent_unordered_map<int, std::shared_ptr<SESSION>> clients;

// Keep players and NPCs in one object table so visibility code can be shared.

SOCKET g_s_socket, g_c_socket;
OVER_EXP g_a_over;

bool is_pc(int object_id)
{
    return object_id >= 0 && object_id < MAX_PLAYERS;
}

bool is_npc(int object_id)
{
    return object_id >= NPC_FIRST_INDEX && object_id < NPC_END_INDEX;
}

bool can_see(const SESSION& from, const SESSION& to)
{
    if (abs(from._x - to._x) > VIEW_RANGE) return false;
    return abs(from._y - to._y) <= VIEW_RANGE;
}

void SESSION::do_random_move()
{
    unordered_set<int> old_vl;


    for (auto& obj : clients)
    {
        auto& pl = obj.second;
        if (ST_INGAME != pl->_state) continue;
        if (true == is_npc(pl->_id)) continue;
        if (can_see(*this, *pl))
            old_vl.insert(pl->_id);
    }


    switch (rand() % 4)
    {
    case 0: if (_x < (WORLD_WIDTH - 1)) _x++; break;
    case 1: if (_x > 0) _x--; break;
    case 2: if (_y < (WORLD_HEIGHT - 1)) _y++; break;
    case 3:if (_y > 0) _y--; break;
    }

    unordered_set<int> new_vl;

    for (auto& obj : clients)
    {
        auto& pl = obj.second;
        if (ST_INGAME != pl->_state) continue;
        if (true == is_npc(pl->_id)) continue;
        if (can_see(*this, *pl))
            new_vl.insert(pl->_id);
    }

    for (auto pl_id : new_vl)
    {
        auto it = clients.find(pl_id);
        if (it == clients.end() || it->second == nullptr) continue;
        auto& target = it->second;

        if (0 == old_vl.count(pl_id))
        {
            // Newly visible to this player.
            target->send_add_player_packet(_id);
        }
        else
        {
            // Still visible.
            target->send_move_packet(_id);
        }
    }

    for (auto pl_id : old_vl)
    {
        if (0 == new_vl.count(pl_id))
        {
            auto it = clients.find(pl_id);
            if (it == clients.end() || it->second == nullptr) continue;

            it->second->send_remove_player_packet(_id);
        }
    }

    if (_id == NPC_FIRST_INDEX)
    {
        auto delay = system_clock::now() - npc_last_move_time;
        //std::cout << "NPC " << _id << " moved. Time since last move: " << duration_cast<milliseconds>(delay).count() << "ms\n";
    }

    npc_last_move_time = system_clock::now();
}

void SESSION::send_move_packet(int c_id)
{
    S2C_MoveObject p{};
    p.object_id = c_id;
    p.size = sizeof(S2C_MoveObject);
    p.type = S2C_MOVE_OBJECT;
    p.x = clients[c_id]->_x;
    p.y = clients[c_id]->_y;
    p.move_time = clients[c_id]->last_move_time;
    do_send(&p);
}

void SESSION::send_add_player_packet(int c_id)
{
    std::shared_ptr<SESSION> pl = clients[c_id];
    if (nullptr == pl) return;

    S2C_AddObject add_packet{};
    add_packet.object_id = c_id;
    add_packet.visual_id = clients[c_id]->visual_id;
    strncpy_s(add_packet.obj_name, MAX_NAME_LEN, clients[c_id]->_name, _TRUNCATE);
    add_packet.size = sizeof(S2C_AddObject);
    add_packet.type = S2C_ADD_OBJECT;
    add_packet.x = clients[c_id]->_x;
    add_packet.y = clients[c_id]->_y;
    add_packet.hp = clients[c_id]->hp;
    add_packet.max_hp = clients[c_id]->max_hp;
    add_packet.exp = clients[c_id]->exp;
    add_packet.level = clients[c_id]->level;

    _vl.lock();
    _view_list.insert(c_id);
    _vl.unlock();

    do_send(&add_packet);
}

void SESSION::send_chat_packet(int p_id, const char* mess)
{
    S2C_ChatMessage packet{};
    packet.size = sizeof(S2C_ChatMessage);
    packet.type = S2C_CHAT_MESSAGE;
    packet.object_id = p_id;
    strcpy_s(packet.message, mess);
    do_send(&packet);
}

void disconnect(int c_id);

void update_view_after_move(int c_id, short new_x, short new_y, int move_time)
{
    auto it = clients.find(c_id);
    if (it == clients.end() || it->second == nullptr) return;
    auto& mover = it->second;

    // 좌표 업데이트
    {
        lock_guard<mutex> ll{ mover->_s_lock };
        mover->last_move_time = move_time;
        mover->_x = clamp_world_x(new_x);
        mover->_y = clamp_world_y(new_y);
    }

    unordered_set<int> near_list;

    mover->_vl.lock();
    unordered_set<int> old_vlist = mover->_view_list;
    mover->_vl.unlock();


    for (auto& cl : clients)
    {
        auto& other = cl.second;   // 상대방 session

        if (other == nullptr) continue;
        if (other->_state != ST_INGAME) continue;
        if (other->_id == c_id) continue;
        if (other->_id == c_id) continue;

        if (can_see(*mover, *other))
            near_list.insert(other->_id);
    }

    mover->send_move_packet(c_id);

    for (auto& pl_id : near_list)
    {
        auto it = clients.find(pl_id);

        if (it == clients.end() || it->second == nullptr) continue;

        auto& target = it->second;

        if (is_pc(pl_id))
        {
            target->_vl.lock();
            if (target->_view_list.count(c_id))
            {
                target->_vl.unlock();
                target->send_move_packet(c_id); 
            }
            else 
            {
                target->_vl.unlock();
                target->send_add_player_packet(c_id); 
            }
        }


        if (old_vlist.count(pl_id) == 0)
        {
            mover->send_add_player_packet(pl_id);
            if (is_npc(pl_id))
                target->wake_up();
        }
    }

    for (auto& pl_id : old_vlist)
    {
        if (0 == near_list.count(pl_id))
        {
            mover->send_remove_player_packet(pl_id);

            if (is_pc(pl_id))
            {
                auto it = clients.find(pl_id);
                if (it != clients.end() && it->second != nullptr)
                {
                    it->second->send_remove_player_packet(c_id);
                }
            }
        }
    }
}

void broadcast_chat_packet(int c_id, const char* message)
{
    clients[c_id]->send_chat_packet(c_id, message);

    clients[c_id]->_vl.lock();
    unordered_set<int> view_list = clients[c_id]->_view_list;
    clients[c_id]->_vl.unlock();

    for (auto& pl : view_list)
    {
        if (false == is_pc(pl)) continue;
        {
            lock_guard<mutex> ll(clients[pl]->_s_lock);
            if (clients[pl]->_state != ST_INGAME) continue;
        }
        clients[pl]->send_chat_packet(c_id, message);
    }
}

bool process_packet(int c_id, char* packet)
{
    const int packet_size = static_cast<unsigned char>(packet[0]);
    if (packet_size < sizeof(unsigned char) + sizeof(PACKET_TYPE))
        return true;

    PACKET_TYPE packet_type;
    memcpy(&packet_type, packet + sizeof(unsigned char), sizeof(packet_type));

    switch (packet_type)
    {
    case C2S_LOGIN: {
        if (packet_size < sizeof(C2S_Login)) return true;

        auto it = clients.find(c_id);
        if (it == clients.end() || it->second == nullptr) return true;
        auto& curr_cl = it->second;

        C2S_Login* p = reinterpret_cast<C2S_Login*>(packet);
        copy_fixed_string(curr_cl->_name, p->username);
        {
            lock_guard<mutex> ll{ curr_cl->_s_lock };
            curr_cl->_x = rand() % WORLD_WIDTH;
            curr_cl->_y = rand() % WORLD_HEIGHT;
            curr_cl->visual_id = DEFAULT_VISUAL_ID;
            curr_cl->hp = DEFAULT_MAX_HP;
            curr_cl->max_hp = DEFAULT_MAX_HP;
            curr_cl->exp = 0;
            curr_cl->level = DEFAULT_LEVEL;
            curr_cl->_state = ST_INGAME;
        }

        //cout << "[LOGIN] ID(" << c_id << ") Name(" << curr_cl->_name
        //    << ") Pos(" << curr_cl->_x << ", " << curr_cl->_y << ") State: INGAME" << endl;

        curr_cl->send_login_info_packet();

        // 주변 플레이어, NPC 처리
        for (auto& cl : clients)
        {
            auto& pl = cl.second;
            if (pl == nullptr) continue;
            if (pl->_id == c_id) continue;

            {
                lock_guard<mutex> ll(pl->_s_lock);
                if (ST_INGAME != pl->_state) continue;
            }

            if (false == can_see(*curr_cl, *pl))
                continue;
            if (is_pc(pl->_id)) pl->send_add_player_packet(c_id);
            curr_cl->send_add_player_packet(pl->_id);

            // Wake NPCs that just entered the player's view.
            if (is_npc(pl->_id))
                pl->wake_up();


        }
        break;
    }
    case C2S_MOVE: {
        if (packet_size < sizeof(C2S_Move)) return true;
        C2S_Move* p = reinterpret_cast<C2S_Move*>(packet);
        update_view_after_move(c_id, p->x, p->y, p->move_time);
        break;
    }
    case C2S_CHAT: {
        if (packet_size < sizeof(C2S_Chat)) return true;
        C2S_Chat* p = reinterpret_cast<C2S_Chat*>(packet);
        char message[MAX_CHAT_MSG_LEN];
        copy_fixed_string(message, p->message);
        broadcast_chat_packet(c_id, message);
        break;
    }
    case C2S_TELEPORT: {
        if (packet_size < sizeof(C2S_Teleport)) return true;
        C2S_Teleport* p = reinterpret_cast<C2S_Teleport*>(packet);
        update_view_after_move(c_id, p->x, p->y, 0);
        break;
    }
    case C2S_ATTACK:
        break;
    case C2S_LOGOUT:
        disconnect(c_id);
        return false;
    default:
        break;
    }

    return true;
}

void disconnect(int c_id)
{
    if (is_npc(c_id)) return;

    auto it = clients.find(c_id);
    if (it == clients.end() || it->second == nullptr) return;
    auto& mover = it->second;

    {
        lock_guard<mutex> ll(mover->_s_lock);
        if (mover->_state == ST_FREE) return;
        mover->_state = ST_FREE;
    }

    mover->_vl.lock();
    unordered_set<int> vl = mover->_view_list;
    mover->_view_list.clear();
    mover->_vl.unlock();

    for (auto& p_id : vl)
    {
        auto target_it = clients.find(p_id);
        if (target_it == clients.end() || target_it->second == nullptr) continue;
        auto& target = target_it->second;

        {
            lock_guard<mutex> ll(target->_s_lock);
            if (target->_state != ST_INGAME) continue;
        }

        if (is_pc(p_id))
        {
            target->send_remove_player_packet(c_id);
        }
    }

    if (mover->_socket != INVALID_SOCKET)
    {
        closesocket(mover->_socket);
        mover->_socket = INVALID_SOCKET; 
    }

    closesocket(clients[c_id]->_socket);
}

void do_npc_random_move(int npc_id)
{
    // Move one NPC and notify nearby players.
    auto npc_it = clients.find(npc_id);
    if (npc_it != clients.end() && npc_it->second != nullptr)
    {
        npc_it->second->do_random_move();
    }
}

void worker_thread(HANDLE h_iocp)
{
    while (true)
    {
        DWORD num_bytes;
        ULONG_PTR key;
        WSAOVERLAPPED* over = nullptr;
        BOOL ret = GetQueuedCompletionStatus(h_iocp, &num_bytes, &key, &over, INFINITE);

        OVER_EXP* ex_over = reinterpret_cast<OVER_EXP*>(over);
        if (FALSE == ret)
        {
            if (ex_over->_comp_type == OP_ACCEPT) cout << "Accept Error";
            else
            {
                cout << "GQCS Error on client[" << key << "]\n";
                disconnect(static_cast<int>(key));
                if (ex_over->_comp_type == OP_SEND) delete ex_over;
                continue;
            }
        }

        if ((0 == num_bytes) && ((ex_over->_comp_type == OP_RECV) || (ex_over->_comp_type == OP_SEND)))
        {
            disconnect(static_cast<int>(key));
            if (ex_over->_comp_type == OP_SEND) delete ex_over;
            continue;
        }

        switch (ex_over->_comp_type)
        {
        case OP_ACCEPT: {

            if (current_user_count++ >= MAX_PLAYERS)
            {
                current_user_count--;
                cout << "No more player can be accepted." << endl;
                closesocket(ex_over->_socket);
            }
            else
            {
                // atomic하게 player_index를 증가시키면서 새로운 클라이언트에게 ID 할당.
                int client_id = player_index++;

                std::shared_ptr<SESSION> new_session = std::make_shared<SESSION>(ex_over->_socket, client_id);
                // 컨테이너에 등록
                clients[client_id] = new_session;

                // iocp에 클라이언트 소켓 등록
                CreateIoCompletionPort(reinterpret_cast<HANDLE>(ex_over->_socket),
                    h_iocp, client_id, 0);

                // recv 시작
                clients[client_id]->do_recv();
            }

            // 다음 클라이언트를 받기 위해 AcceptEx 호출.
            ex_over->_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
            ZeroMemory(&ex_over->_over, sizeof(ex_over->_over));    // over 구조체 초기화

            AcceptEx(g_s_socket, ex_over->_socket, ex_over->_send_buf, 0,
                sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
                NULL, &ex_over->_over);
            break;
        }
        case OP_RECV: {

            // 클라이언트가 연결을 끊고 나간경우
            if (0 == num_bytes)
            {
                disconnect(key);
                break;
            }

            auto it = clients.find(static_cast<int>(key));

            // 널체크 및 클라이언트 존재 여부 확인
            if (it == clients.end() || it->second == nullptr)
            {
                break;
            }

            std::shared_ptr<SESSION> session = it->second;

            int remain_data = num_bytes + session->_prev_remain;
            char* p = ex_over->_send_buf;
            bool keep_connected = true;

            while (remain_data > 0)
            {
                int packet_size = static_cast<unsigned char>(p[0]);
                if (packet_size == 0)
                {
                    disconnect(static_cast<int>(key));
                    keep_connected = false;
                    break;
                }
                if (packet_size <= remain_data)
                {
                    keep_connected = process_packet(static_cast<int>(key), p);
                    if (false == keep_connected)
                        break;
                    p = p + packet_size;
                    remain_data = remain_data - packet_size;
                }
                else break;
            }

            if (false == keep_connected)
                break;

            clients[key]->_prev_remain = remain_data;
            if (remain_data > 0)
            {
                memcpy(ex_over->_send_buf, p, remain_data);
            }

            clients[key]->do_recv();
            break;
        }
        case OP_SEND:
            delete ex_over;
            break;
        case OP_NPCMOVE: {
            // npc 이동은 워커 스레드에서 처리..
            delete ex_over;
            int npc_id = static_cast<int>(key);

            // 특정 타겟을 찾아야 할 때 안전하게 find 로 찾기
            auto npc_it = clients.find(npc_id);
            if (npc_it == clients.end() || npc_it->second == nullptr)
            {
                break;
            }
            auto& npc_session = npc_it->second;

            do_npc_random_move(npc_id);

            // Check whether any player can still see this NPC.
            bool has_nearby_player = false;

            for (auto& item : clients)
            {
                int p_id = item.first;
                std::shared_ptr<SESSION> p_session = item.second;

                // NPC는 유저가 아니므로 패스
                if (is_npc(p_id)) continue;

                // 세션이 안전하게 존재하고 인게임 상태인지 확인
                if (p_session != nullptr && p_session->_state == ST_INGAME)
                {
                    // 시야 체크
                    if (can_see(*npc_session, *p_session))
                    {
                        has_nearby_player = true;
                        break;
                    }
                }
            }

            if (has_nearby_player)
            {
                // Schedule the next movement event.
                event_type ev;
                ev.event_id = EVENT_MOVE;
                ev.obj_id = npc_id;
                ev.target_id = -1;
                ev.wakeup_time = system_clock::now() + milliseconds(MOVE_COOL_TIME);
                timer_queue.push(ev);
            }
            else
            {
                // No nearby player: sleep until wake_up() is called again.
                clients[npc_id]->_active_npc = false;
            }
            break;
        }
        }
    }
}

void InitializeNPC()
{
    cout << "NPC initialize begin.\n";
    for (int i = NPC_FIRST_INDEX; i < NPC_END_INDEX; ++i)
    {
        // NPC 객체를 생성하여 컨테이너에 등록.
        clients[i] = std::make_shared<SESSION>(i);
    }
    cout << "NPC initialize end.\n";
}

void timer_thread()
{
    while (true)
    {
        event_type ev;
        if (timer_queue.try_pop(ev))
        {
            auto now = system_clock::now();
            if (ev.wakeup_time <= now)
            {
                switch (ev.event_id)
                {
                case EVENT_MOVE:
                    OVER_EXP* move_over = new OVER_EXP;
                    move_over->_comp_type = OP_NPCMOVE; // Route timer events through IOCP.
                    PostQueuedCompletionStatus(h_iocp, -1, ev.obj_id, &move_over->_over);
                    break;
                }
            }
            else
            {
                // Not due yet; push it back and wait briefly.
                timer_queue.push(ev);
                this_thread::sleep_for(chrono::milliseconds(1));
            }
        }
        else
        {
            this_thread::sleep_for(chrono::milliseconds(1));
        }
    }
}


int main()
{
    WSADATA WSAData;
    WSAStartup(MAKEWORD(2, 2), &WSAData);
    g_s_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    SOCKADDR_IN server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
    ::bind(g_s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    ::listen(g_s_socket, SOMAXCONN);
    SOCKADDR_IN cl_addr;
    int addr_size = sizeof(cl_addr);

    InitializeNPC();

    h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
    CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_s_socket), h_iocp, 9999, 0);

    g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    g_a_over._comp_type = OP_ACCEPT;
    g_a_over._socket = g_c_socket; 

    AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0,
        addr_size + 16, addr_size + 16,
        NULL, &g_a_over._over);

    vector<thread> worker_threads;
    thread timer_th(timer_thread);   // Use timer_thread for NPC movement scheduling.
    int num_threads = std::thread::hardware_concurrency();
    if (num_threads <= 0) num_threads = 1;
    for (int i = 0; i < num_threads; ++i)
        worker_threads.emplace_back(worker_thread, h_iocp);
    for (auto& th : worker_threads)
        th.join();
    timer_th.join();
    closesocket(g_s_socket);
    WSACleanup();
}
