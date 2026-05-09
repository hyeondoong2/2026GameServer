#include <iostream>
#include <array>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <chrono>
#include <concurrent_priority_queue.h>
#include <tbb/concurrent_unordered_map.h>
#include "protocol.h"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")
using namespace std;
using namespace std::chrono;

constexpr int MOVE_COOL_TIME = 1000; // ms

constexpr int EVENT_MOVE = 1;
constexpr int VIEW_RANGE = 5;

constexpr int SECTOR_SIZE = VIEW_RANGE + 5;
constexpr int SECTOR_ROWS = W_WIDTH / SECTOR_SIZE + 1;
constexpr int SECTOR_COLS = W_HEIGHT / SECTOR_SIZE + 1;

struct SECTOR
{
    std::mutex m_mutex;
    std::unordered_set<int> players;
};

SECTOR sectors[SECTOR_ROWS][SECTOR_COLS];

void add_to_sector(int id, int sx, int sy)
{
    lock_guard<mutex> ll(sectors[sx][sy].m_mutex);
    sectors[sx][sy].players.insert(id);
}

void remove_from_sector(int id, int sx, int sy)
{
    lock_guard<mutex> ll(sectors[sx][sy].m_mutex);
    sectors[sx][sy].players.erase(id);
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

    OVER_EXP()
    {
        _wsabuf.len = BUF_SIZE;
        _wsabuf.buf = _send_buf;
        _comp_type = OP_RECV;
        ZeroMemory(&_over, sizeof(_over));
    }
    OVER_EXP(char* packet)
    {
        _wsabuf.len = packet[0];
        _wsabuf.buf = _send_buf;
        ZeroMemory(&_over, sizeof(_over));
        _comp_type = OP_SEND;
        memcpy(_send_buf, packet, packet[0]);
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
    char	_name[NAME_SIZE];
    int		_prev_remain;
    unordered_set <int> _view_list;
    mutex	_vl;
    int last_move_time;
    std::atomic<bool> _active_npc;
    system_clock::time_point npc_last_move_time;

    // sector
    int _sector_x;
    int _sector_y;

public:
    SESSION()
    {
        _id = -1;
        _socket = 0;
        _x = _y = 0;
        _name[0] = 0;
        _state = ST_FREE;
        _prev_remain = 0;
        _sector_x = _x / SECTOR_SIZE;
        _sector_y = _y / SECTOR_SIZE;
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

    void do_send(void* packet)
    {
        OVER_EXP* sdata = new OVER_EXP{ reinterpret_cast<char*>(packet) };
        WSASend(_socket, &sdata->_wsabuf, 1, 0, 0, &sdata->_over, 0);
    }
    void send_login_info_packet()
    {
        SC_LOGIN_INFO_PACKET p;
        p.id = _id;
        p.size = sizeof(SC_LOGIN_INFO_PACKET);
        p.type = SC_LOGIN_INFO;
        p.x = _x;
        p.y = _y;
        do_send(&p);
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
        SC_REMOVE_OBJECT_PACKET p;
        p.id = c_id;
        p.size = sizeof(p);
        p.type = SC_REMOVE_OBJECT;
        do_send(&p);
    }
    void do_random_move();
    void heart_beat()
    {
        // NPC의 경우, 일정 시간마다 랜덤한 방향으로 이동하는 기능을 구현한다.
        // 이동한 후에는, 이동한 위치를 주변 플레이어들에게 알려준다.
        do_random_move();
    }

    void wake_up()
    {
        // _active_npc가 false일 때만 true로 바꾸고 타이머 등록 (중복 등록 방지)
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

SOCKET g_s_socket, g_c_socket;
OVER_EXP g_a_over;

bool is_pc(int object_id)
{
    return object_id < MAX_USER;
}

bool is_npc(int object_id)
{
    return !is_pc(object_id);
}

int get_new_client_id()
{
    for (int i = 0; i < MAX_USER; ++i)
    {
        auto it = clients.find(i);
        if (it == clients.end() || it->second == nullptr)
            return i;  // map에 없으면 빈 슬롯
        lock_guard<mutex> ll{ it->second->_s_lock };
        if (it->second->_state == ST_FREE)
            return i;
    }
    return -1;
}


unordered_set<int> get_near_ids(int id)
{
    auto it = clients.find(id);
    if (it == clients.end()) return {};
    auto& s = it->second;

    int sx = s->_sector_x;
    int sy = s->_sector_y;

    unordered_set<int> result;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
        {
            int nx = sx + dx;
            int ny = sy + dy;
            if (nx < 0 || nx >= SECTOR_ROWS) continue;
            if (ny < 0 || ny >= SECTOR_COLS) continue;

            lock_guard<mutex> ll(sectors[nx][ny].m_mutex);
            for (int pid : sectors[nx][ny].players)
                result.insert(pid);
        }
    return result;
}



bool can_see(int from, int to)
{
    auto from_client = clients.find(from);
    auto to_client = clients.find(to);

    auto& from_session = from_client->second;
    auto& to_session = to_client->second;

    if (abs(from_session->_x - to_session->_x) > VIEW_RANGE) return false;
    return abs(from_session->_y - to_session->_y) <= VIEW_RANGE;
}

void SESSION::do_random_move()
{
    unordered_set<int> old_vl;

    auto old_candidates = get_near_ids(_id);  
    for (auto& pid : old_candidates)
    {
        auto it = clients.find(pid);
        if (it == clients.end() || it->second == nullptr) continue;
        auto& pl = it->second;
        if (ST_INGAME != pl->_state) continue;
        if (true == is_npc(pl->_id)) continue;
        if (true == can_see(_id, pl->_id))
            old_vl.insert(pl->_id);
    }


    switch (rand() % 4)
    {
    case 0: if (_x < (W_WIDTH - 1)) _x++; break;
    case 1: if (_x > 0) _x--; break;
    case 2: if (_y < (W_HEIGHT - 1)) _y++; break;
    case 3:if (_y > 0) _y--; break;
    }

    // 이동 후 섹터 갱신
    int old_sx = _sector_x;
    int old_sy = _sector_y;
    int new_sx = _x / SECTOR_SIZE;
    int new_sy = _y / SECTOR_SIZE;

    if (old_sx != new_sx || old_sy != new_sy)
    {
        remove_from_sector(_id, old_sx, old_sy);
        _sector_x = new_sx;
        _sector_y = new_sy;
        add_to_sector(_id, new_sx, new_sy);
    }


    unordered_set<int> new_vl;
    auto new_candidates = get_near_ids(_id); 
    for (auto& pid : new_candidates)
    {
        auto it = clients.find(pid);
        if (it == clients.end() || it->second == nullptr) continue;
        auto& pl = it->second;
        if (ST_INGAME != pl->_state) continue;
        if (true == is_npc(pl->_id)) continue;
        if (true == can_see(_id, pl->_id))
            new_vl.insert(pl->_id);
    }

    for (auto pl : new_vl)
    {
        auto it = clients.find(pl);
        if (it == clients.end() || it->second == nullptr) continue;
        auto& target = it->second;

        if (0 == old_vl.count(pl))
        {
            // 플레이어의 시야에 등장
            target->send_add_player_packet(_id);
        }
        else
        {
            // 플레이어가 계속 보고 있음.
            target->send_move_packet(_id);
        }
    }

    for (auto pl : old_vl)
    {
        if (0 == new_vl.count(pl))
        {
            auto it = clients.find(pl);
            if (it == clients.end() || it->second == nullptr) continue;

            it->second->send_remove_player_packet(_id);
        }

    }

    if (_id == MAX_USER)
    {
        auto delay = system_clock::now() - npc_last_move_time;
        //std::cout << "NPC " << _id << " moved. Time since last move: " << duration_cast<milliseconds>(delay).count() << "ms\n";
    }

    npc_last_move_time = system_clock::now();
}

void SESSION::send_move_packet(int c_id)
{
    auto it = clients.find(c_id);
    if (it == clients.end() || it->second == nullptr) return;
    auto& target = it->second;

    SC_MOVE_OBJECT_PACKET p;
    p.id = c_id;
    p.size = sizeof(SC_MOVE_OBJECT_PACKET);
    p.type = SC_MOVE_OBJECT;
    p.x = target->_x;
    p.y = target->_y;
    p.move_time = target->last_move_time;
    do_send(&p);
}

void SESSION::send_add_player_packet(int c_id)
{
    auto it = clients.find(c_id);
    if (it == clients.end() || it->second == nullptr) return;
    auto& target = it->second;

    SC_ADD_OBJECT_PACKET add_packet;
    add_packet.id = c_id;
    strcpy_s(add_packet.name, target->_name);
    add_packet.size = sizeof(add_packet);
    add_packet.type = SC_ADD_OBJECT;
    add_packet.x = target->_x;
    add_packet.y = target->_y;
    _vl.lock();
    _view_list.insert(c_id);
    _vl.unlock();
    do_send(&add_packet);
}

void SESSION::send_chat_packet(int p_id, const char* mess)
{
    SC_CHAT_PACKET packet;
    packet.id = p_id;
    packet.size = sizeof(packet);
    packet.type = SC_CHAT;
    strcpy_s(packet.mess, mess);
    do_send(&packet);
}

void process_packet(int c_id, char* packet)
{
    // c_id 존재는 보장됨. clients[c_id]로 접근 가능
    switch (packet[1])
    {
    case CS_LOGIN: {
        CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);
        strcpy_s(clients[c_id]->_name, p->name);
        {
            lock_guard<mutex> ll{ clients[c_id]->_s_lock };
            clients[c_id]->_x = rand() % W_WIDTH;
            clients[c_id]->_y = rand() % W_HEIGHT;
            clients[c_id]->_state = ST_INGAME;
            clients[c_id]->_sector_x = clients[c_id]->_x / SECTOR_SIZE;
            clients[c_id]->_sector_y = clients[c_id]->_y / SECTOR_SIZE;
            add_to_sector(c_id, clients[c_id]->_sector_x, clients[c_id]->_sector_y);
        }
        clients[c_id]->send_login_info_packet();

        for (auto& pl : clients)
        {
            auto& other = pl.second;
            {
                lock_guard<mutex> ll(other->_s_lock);
                if (ST_INGAME != other->_state) continue;
            }

            if (other->_id == c_id) continue;
            if (false == can_see(c_id, other->_id))
                continue;
            if (is_pc(other->_id)) other->send_add_player_packet(c_id);
            clients[c_id]->send_add_player_packet(other->_id);

            // 시야에 들어온 NPC를 깨움
            if (is_npc(other->_id))
                other->wake_up();
        }
        break;
    }
    case CS_MOVE: {
        CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);
        clients[c_id]->last_move_time = p->move_time;
        short x = clients[c_id]->_x;
        short y = clients[c_id]->_y;
        switch (p->direction)
        {
        case 0: if (y > 0) y--; break;
        case 1: if (y < W_HEIGHT - 1) y++; break;
        case 2: if (x > 0) x--; break;
        case 3: if (x < W_WIDTH - 1) x++; break;
        }

        int old_sx = clients[c_id]->_sector_x;
        int old_sy = clients[c_id]->_sector_y;
        int new_sx = x / SECTOR_SIZE;
        int new_sy = y / SECTOR_SIZE;

        clients[c_id]->_x = x;
        clients[c_id]->_y = y;

        if (old_sx != new_sx || old_sy != new_sy)
        {
            remove_from_sector(c_id, old_sx, old_sy);
            clients[c_id]->_sector_x = new_sx;
            clients[c_id]->_sector_y = new_sy;
            add_to_sector(c_id, new_sx, new_sy);
        }

        unordered_set<int> near_list;
        clients[c_id]->_vl.lock();
        unordered_set<int> old_vlist = clients[c_id]->_view_list;
        clients[c_id]->_vl.unlock();

        auto candidates = get_near_ids(c_id);
        for (auto& pl : candidates)
        {
            auto it = clients.find(pl);
            if (it == clients.end() || it->second == nullptr) continue;
            auto& other = it->second;
            if (other->_state != ST_INGAME) continue;
            if (other->_id == c_id) continue;
            if (can_see(c_id, other->_id))
                near_list.insert(other->_id);
        }

        clients[c_id]->send_move_packet(c_id);

        for (auto& pl : near_list)
        {
            auto it = clients.find(pl);
            if (it == clients.end() || it->second == nullptr) continue;
            auto& cpl = it->second;

            if (is_pc(pl))
            {
                cpl->_vl.lock();
                if (cpl->_view_list.count(c_id))
                {
                    cpl->_vl.unlock();
                    cpl->send_move_packet(c_id);
                }
                else
                {
                    cpl->_vl.unlock();
                    cpl->send_add_player_packet(c_id);
                }
            }

            if (old_vlist.count(pl) == 0)
            {
                clients[c_id]->send_add_player_packet(pl);
                // 새로 시야에 들어온 NPC를 깨움
                if (is_npc(pl))
                  cpl->wake_up();
            }
        }

        for (auto& pl : old_vlist)
            if (0 == near_list.count(pl))
            {
                clients[c_id]->send_remove_player_packet(pl);
                if (is_pc(pl))
                {
                    auto it = clients.find(pl);
                    if (it == clients.end() || it->second == nullptr) continue;
                    it->second->send_remove_player_packet(c_id);
                }
            }
        break;
    }
    }
}

void disconnect(int c_id)
{
    remove_from_sector(c_id, clients[c_id]->_sector_x, clients[c_id]->_sector_y);

    clients[c_id]->_vl.lock();
    unordered_set <int> vl = clients[c_id]->_view_list;
    clients[c_id]->_vl.unlock();

    for (auto& p_id : vl)
    {
        if (is_npc(p_id)) continue;
        auto it = clients.find(p_id);
        if (it == clients.end() || it->second == nullptr) continue;
        auto& pl = it->second;

        {
            lock_guard<mutex> ll(pl->_s_lock);
            if (ST_INGAME != pl->_state) continue;
        }
        if (pl->_id == c_id) continue;
        pl->send_remove_player_packet(c_id);
    }
    closesocket(clients[c_id]->_socket);

    {
        lock_guard<mutex> ll(clients[c_id]->_s_lock);
        clients[c_id]->_state = ST_FREE;
    }
}

void do_npc_random_move(int npc_id)
{
    // NPC의 랜덤 이동을 처리하는 함수
    // npc_id에 해당하는 NPC를 랜덤한 방향으로 이동시키고, 주변 플레이어들에게 이동 정보를 알려준다.
    clients[npc_id]->do_random_move();
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
            int client_id = get_new_client_id();
            if (client_id != -1)

            {
                clients[client_id] = std::make_shared<SESSION>();
                {
                    lock_guard<mutex> ll(clients[client_id]->_s_lock);
                    clients[client_id]->_state = ST_ALLOC;
                }
                clients[client_id]->_x = 0;
                clients[client_id]->_y = 0;
                clients[client_id]->_id = client_id;
                clients[client_id]->_name[0] = 0;
                clients[client_id]->_prev_remain = 0;
                clients[client_id]->_socket = g_c_socket;
                CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_c_socket),
                    h_iocp, client_id, 0);
                g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
                clients[client_id]->do_recv();
            }
            ZeroMemory(&g_a_over._over, sizeof(g_a_over._over));
            int addr_size = sizeof(SOCKADDR_IN);
            AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0, addr_size + 16, addr_size + 16, 0, &g_a_over._over);
            break;
        }
        case OP_RECV: {
            int remain_data = num_bytes + clients[key]->_prev_remain;
            char* p = ex_over->_send_buf;
            while (remain_data > 0)
            {
                int packet_size = p[0];
                if (packet_size <= remain_data)
                {
                    process_packet(static_cast<int>(key), p);
                    p = p + packet_size;
                    remain_data = remain_data - packet_size;
                }
                else break;
            }
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
            delete ex_over;
            int npc_id = static_cast<int>(key);
            do_npc_random_move(npc_id);

            // 시야 내에 플레이어가 있는지 확인
            bool has_nearby_player = false;

            auto candidates = get_near_ids(npc_id);
            for (auto& pl : candidates)
            {
                auto player = clients[pl];
      
                if (player == nullptr) continue;
                if (is_npc(player->_id)) continue;

                if (player->_state == ST_INGAME && can_see(npc_id, player->_id))
                {
                    has_nearby_player = true;
                    break;
                }
            }

            if (has_nearby_player)
            {
                // 다음 이동 이벤트 재등록
                event_type ev;
                ev.event_id = EVENT_MOVE;
                ev.obj_id = npc_id;
                ev.target_id = -1;
                ev.wakeup_time = system_clock::now() + milliseconds(MOVE_COOL_TIME);
                timer_queue.push(ev);
            }
            else
            {
                // 시야 내 플레이어 없음 → AI 비활성화 (다음 wake_up() 호출까지 대기)
                auto it = clients.find(npc_id);
                if (it != clients.end() && it->second != nullptr)
                    it->second->_active_npc = false;
            }
            break;
        }
        }
    }
}

void InitializeNPC()
{
    cout << "NPC intialize begin.\n";
    for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i)
    {
        clients[i] = std::make_shared<SESSION>();
        clients[i]->_x = rand() % W_WIDTH;
        clients[i]->_y = rand() % W_HEIGHT;
        clients[i]->_id = i;
        sprintf_s(clients[i]->_name, "NPC%d", i);
        clients[i]->_state = ST_INGAME;
        clients[i]->last_move_time = 0;
        clients[i]->npc_last_move_time = system_clock::now();
        clients[i]->_sector_x = clients[i]->_x / SECTOR_SIZE;
        clients[i]->_sector_y = clients[i]->_y / SECTOR_SIZE;
        add_to_sector(i, clients[i]->_sector_x, clients[i]->_sector_y);
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
                    move_over->_comp_type = OP_NPCMOVE; // 이동 이벤트는 OP_SEND로 처리
                    PostQueuedCompletionStatus(h_iocp, -1, ev.obj_id, &move_over->_over); // 이동 이벤트를 워커 스레드로 전달
                    break;
                }
            }
            else
            {
                // 아직 시간이 안 됐으면 다시 큐에 넣음
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
    server_addr.sin_port = htons(PORT_NUM);
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
    AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0, addr_size + 16, addr_size + 16, 0, &g_a_over._over);

    vector<thread> worker_threads;
    thread timer_th(timer_thread);   // HB_thread 대신 timer_thread 사용
    int num_threads = std::thread::hardware_concurrency();
    for (int i = 0; i < num_threads; ++i)
        worker_threads.emplace_back(worker_thread, h_iocp);
    for (auto& th : worker_threads)
        th.join();
    timer_th.join();
    closesocket(g_s_socket);
    WSACleanup();
}
