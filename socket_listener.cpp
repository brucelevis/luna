﻿/*
** repository: https://github.com/trumanzhao/luna
** trumanzhao, 2016-11-01, trumanzhao@foxmail.com
*/

#ifdef _MSC_VER
#include <Winsock2.h>
#include <Ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#endif
#ifdef __linux
#include <sys/epoll.h>
#endif
#ifdef __APPLE__
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#endif
#if defined(__linux) || defined(__APPLE__)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <assert.h>
#include "tools.h"
#include "var_int.h"
#include "socket_mgr.h"
#include "socket_listener.h"

#ifdef _MSC_VER
socket_listener::socket_listener(LPFN_ACCEPTEX accept_func, LPFN_GETACCEPTEXSOCKADDRS addrs_func)
{
	m_accept_func = accept_func;
	m_addrs_func = addrs_func;
	memset(m_nodes, 0, sizeof(m_nodes));
	for (auto& node : m_nodes)
	{
		node.fd = INVALID_SOCKET;
	}
}
#endif

socket_listener::~socket_listener()
{
#ifdef _MSC_VER
	for (auto& node : m_nodes)
	{
		if (node.fd != INVALID_SOCKET)
		{
			close_socket_handle(node.fd);
			node.fd = INVALID_SOCKET;
		}
	}
#endif

	if (m_socket != INVALID_SOCKET)
	{
		close_socket_handle(m_socket);
		m_socket = INVALID_SOCKET;
	}
}

bool socket_listener::setup(socket_t fd)
{
	m_socket = fd;
	return true;
}

bool socket_listener::update(socket_manager* mgr, int64_t)
{
	if (m_closed && m_socket != INVALID_SOCKET)
	{
		close_socket_handle(m_socket);
		m_socket = INVALID_SOCKET;
	}

#ifdef _MSC_VER
	if (m_ovl_ref == 0 && !m_closed)
	{
		for (auto& node : m_nodes)
		{
			if (node.fd == INVALID_SOCKET)
			{
				queue_accept(mgr, &node.ovl);
			}
		}
	}
#endif

	if (m_closed)
	{
#ifdef _MSC_VER
		return m_ovl_ref != 0;
#endif

#if defined(__linux) || defined(__APPLE__)
		return false;
#endif
	}
	return true;
}

#ifdef _MSC_VER
void socket_listener::on_complete(socket_manager* mgr, WSAOVERLAPPED* ovl)
{
	m_ovl_ref--;
	if (m_closed)
		return;

	listen_node* node = CONTAINING_RECORD(ovl, listen_node, ovl);
	assert(node >= m_nodes && node < m_nodes + _countof(m_nodes));
	assert(node->fd != INVALID_SOCKET);

	sockaddr* local_addr = nullptr;
	sockaddr* remote_addr = nullptr;
	int local_addr_len = 0;
	int remote_addr_len = 0;
	char ip[INET6_ADDRSTRLEN];

	(*m_addrs_func)(node->buffer, 0, sizeof(node->buffer[0]), sizeof(node->buffer[2]), &local_addr, &local_addr_len, &remote_addr, &remote_addr_len);
	get_ip_string(ip, sizeof(ip), remote_addr, (size_t)remote_addr_len);

	set_none_block(node->fd);

	auto token = mgr->accept_stream(node->fd, ip);
	if (token == 0)
	{
		close_socket_handle(node->fd);
		node->fd = INVALID_SOCKET;
		m_closed = true;
		m_error_cb("accept_stream_failed");
		return;
	}

	node->fd = INVALID_SOCKET;
	m_accept_cb(token);
	queue_accept(mgr, ovl);
}

void socket_listener::queue_accept(socket_manager* mgr, WSAOVERLAPPED* ovl)
{
	listen_node* node = CONTAINING_RECORD(ovl, listen_node, ovl);

	assert(node >= m_nodes && node < m_nodes + _countof(m_nodes));
	assert(node->fd == INVALID_SOCKET);

	sockaddr_storage listen_addr;
	socklen_t listen_addr_len = sizeof(listen_addr);
	getsockname(m_socket, (sockaddr*)&listen_addr, &listen_addr_len);

	while (!m_closed)
	{
		memset(ovl, 0, sizeof(*ovl));
		// 注,AF_INET6本身是可以支持ipv4的,但是...需要win10以上版本,win7不支持, 所以这里取listen_addr
		node->fd = socket(listen_addr.ss_family, SOCK_STREAM, IPPROTO_IP);
		if (node->fd == INVALID_SOCKET)
		{
			m_closed = true;
			m_error_cb("new_socket_failed");
			return;
		}

		set_none_block(node->fd);

		DWORD bytes = 0;
		static_assert(sizeof(sockaddr_storage) >= sizeof(sockaddr_in6) + 16, "buffer too small");
		auto ret = (*m_accept_func)(m_socket, node->fd, node->buffer, 0, sizeof(node->buffer[0]), sizeof(node->buffer[1]), &bytes, ovl);
		if (!ret)
		{
			int err = get_socket_error();
			if (err != ERROR_IO_PENDING)
			{
				char txt[MAX_ERROR_TXT];
				get_error_string(txt, sizeof(txt), err);
				close_socket_handle(node->fd);
				node->fd = INVALID_SOCKET;
				m_closed = true;
				m_error_cb(txt);
				return;
			}
			m_ovl_ref++;
			return;
		}

		sockaddr* local_addr = nullptr;
		sockaddr* remote_addr = nullptr;
		int local_addr_len = 0;
		int remote_addr_len = 0;
		char ip[INET6_ADDRSTRLEN];

		(*m_addrs_func)(node->buffer, 0, sizeof(node->buffer[0]), sizeof(node->buffer[2]), &local_addr, &local_addr_len, &remote_addr, &remote_addr_len);
		get_ip_string(ip, sizeof(ip), remote_addr, (size_t)remote_addr_len);

		auto token = mgr->accept_stream(node->fd, ip);
		if (token == 0)
		{
			close_socket_handle(node->fd);
			node->fd = INVALID_SOCKET;
			m_closed = true;
			m_error_cb("accept_stream_failed");
			return;
		}

		node->fd = INVALID_SOCKET;
		m_accept_cb(token);
	}
}
#endif

#if defined(__linux) || defined(__APPLE__)
void socket_listener::on_complete(socket_manager* mgr, bool can_read, bool can_write)
{
    while (!m_closed)
    {
		sockaddr_storage addr;
		socklen_t addr_len = (socklen_t)sizeof(addr);
		char ip[INET6_ADDRSTRLEN];

		socket_t fd = accept(m_socket, (sockaddr*)&addr, &addr_len);
        if (fd == INVALID_SOCKET)
            break;

		get_ip_string(ip, sizeof(ip), &addr, (size_t)addr_len);

        set_none_block(fd);

        auto token = mgr->accept_stream(fd, ip);
        if (token != 0)
        {
            m_accept_cb(token);
        }
        else
        {
            // TODO: 这种情况,真的要关闭么?
            close_socket_handle(fd);
            m_closed = true;
            m_error_cb("accept_stream_failed");
        }
    }
}
#endif


