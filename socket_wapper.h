﻿/*
** repository: https://github.com/trumanzhao/luna
** trumanzhao, 2016-11-01, trumanzhao@foxmail.com
*/

#pragma once
#include <memory>
#include "socket_io.h"
#include "luna.h"
#include "lua_archiver.h"
#include "io_buffer.h"

struct lua_socket_mgr
{
public:
	~lua_socket_mgr();
	bool setup(lua_State* L, int max_fd, size_t buffer_size, size_t compress_threhold);
	void wait(int ms) { m_mgr->wait(ms); }
	int listen(lua_State* L);
	int connect(lua_State* L);

private:
	lua_State* m_lvm = nullptr;
	std::shared_ptr<lua_archiver> m_archiver;
	std::shared_ptr<io_buffer> m_ar_buffer;
	std::shared_ptr<io_buffer> m_lz_buffer;
	std::shared_ptr<socket_mgr> m_mgr;

public:
	DECLARE_LUA_CLASS(lua_socket_mgr);
};

// 注意,因为包装层的listener,stream析构的时候,已经close了token
// 所以不存在相关事件(accept, package, error...)触发时,相应的wapper对象失效的问题
// 因为close之后,这些事件不可能触发嘛:)

struct lua_socket_node
{
	lua_socket_node(int token, lua_State* L, std::shared_ptr<socket_mgr>& mgr, std::shared_ptr<lua_archiver>& ar, std::shared_ptr<io_buffer>& ar_buffer, std::shared_ptr<io_buffer>& lz_buffer);
	~lua_socket_node();

	int call(lua_State* L);
	void close();

private:
	void on_recv(char* data, size_t data_len);

	int m_token = 0;
	lua_State* m_lvm = nullptr;
	std::shared_ptr<socket_mgr> m_mgr;
	std::shared_ptr<lua_archiver> m_archiver;
	std::shared_ptr<io_buffer> m_ar_buffer;
	std::shared_ptr<io_buffer> m_lz_buffer;

public:
	DECLARE_LUA_CLASS(lua_socket_listener);
};

int lua_create_socket_mgr(lua_State* L);
