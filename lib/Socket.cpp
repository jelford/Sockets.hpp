#include <iostream>     // cerr

#include <memory>       // unique_ptr

#include <sstream>      // Stringbuilder

#include <algorithm>    // std::find

#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <sys/select.h>
#include <sys/poll.h>

#include "Socket.hpp"

jelford::SocketException::SocketException(const int _errno, const jelford::Socket* socket) :  
        _errno(_errno), m_socket(socket) { }

const char* jelford::SocketException::what()
{
    std::stringstream msg;
    msg << jelford::socket_get_error(_errno);
    msg << " (Errno: " << _errno << ")";
    return msg.str().c_str();
}

const jelford::Socket* jelford::SocketException::retrieve_socket() const
{
    return m_socket;
}

jelford::SocketTimeoutException::SocketTimeoutException(Socket* s) : SocketException::SocketException(0, s)
{ }

const char* jelford::SocketTimeoutException::what()
{
    return "Timeout occurred on socket operation";
}

const char* jelford::socket_get_error(int _errno)
{
    switch(_errno) 
    {
        case EBADF:
            return "EBADF: Bad file descriptor";
        case ENFILE:
            return "ENFILE: File table overflow";
        case EINVAL:
            return "EINVAL: Invalid argument";
        case EMFILE:
            return "EMFILE: Too many open files";
        case ESPIPE:
            return "ESPIPE: Illegal seek";
        case EWOULDBLOCK:
            return "EWOULDBLOCK: Operation would block";
        case EINPROGRESS:
            return "EINPROGRESS: Operation now in progress";
        case EADDRINUSE:
            return "EADDRINUSE: Address is already in use";
        case ENOTCONN:
            return "ENOTCONN: Transport endpoint not connected";
        case ECONNREFUSED:
            return "ECONNREFUSED: Connection refused";
        case EISCONN:
            return "EISCONN: Transport endpoint is already connected";
        default:
            return "Unknown socket error";
    }
}

int jelford::Socket::identify() const
{
    return m_socket_descriptor;
}

bool jelford::Socket::is_listening() const
{
    int val;
    socklen_t len = sizeof(val);
    if (::getsockopt(m_socket_descriptor, SOL_SOCKET, SO_ACCEPTCONN, &val, &len) == -1)
        /* Oh dear! This really ought never to happen! */
        throw std::exception();
    else if (val)
        return true;
    else
        return false;
}

jelford::Socket::Socket(int socket_family, int socket_type, int protocol) throw(SocketException) :
    is_nonblocking(false)
{
    m_socket_descriptor = ::socket(socket_family, socket_type, protocol);
    if (m_socket_descriptor < 0)
    {
        throw std::unique_ptr<SocketException>(new SocketException(errno, this));
    }
    std::cerr << m_socket_descriptor << ": initialized" << std::endl;
}

jelford::Socket::Socket(int file_descriptor, bool nonblocking) : 
    m_socket_descriptor(file_descriptor),
    is_nonblocking(nonblocking)
{
    if (file_descriptor <= 0)
    {
        std::cerr << "oopsy! Looks like that (" << m_socket_descriptor << ") isn't a great file descriptor... ";
        if (file_descriptor == 0)
            std::cerr << "(it looks like you're trying to treat stdin as a socket) ";
        std::cerr << " Errno: " << errno << " (which says: " << socket_get_error(errno) << ") ";
        std::cerr << "I'll allow it though, since it might be what you wanted." << std::endl;
    }

    std::cerr << m_socket_descriptor << ": initialized" << std::endl;
}

jelford::Socket::Socket(Socket&& other) : 
    m_socket_descriptor(other.m_socket_descriptor), is_nonblocking(other.is_nonblocking)
{
    // Don't close it when the old Socket is de-allocated
    other.m_socket_descriptor = -1;
}

jelford::Socket& jelford::Socket::operator=(Socket&& other)
{
    this->m_socket_descriptor = other.m_socket_descriptor;
    this->is_nonblocking = other.is_nonblocking;
    other.m_socket_descriptor = -1;
    return *this;
}

void jelford::Socket::set_reuse(bool should_reuse)
{
    int optvalue = should_reuse;
    ::setsockopt(m_socket_descriptor, SOL_SOCKET, SO_REUSEADDR, &optvalue, sizeof(optvalue));
}

void jelford::Socket::bind_to(sockaddr_in socket_address)
{
    if (::bind(m_socket_descriptor, 
                    reinterpret_cast<sockaddr*>(&socket_address), sizeof(socket_address)) < 0)
        throw std::unique_ptr<SocketException>(new SocketException(errno, this));
}

void jelford::Socket::listen(int backlog_size)
{
    if (::listen(m_socket_descriptor, backlog_size) < 0)
        throw std::unique_ptr<SocketException>(new SocketException(errno, this));
}

jelford::Socket jelford::Socket::accept(sockaddr* addr, socklen_t* addrlen)
{
    auto fd = ::accept(m_socket_descriptor, addr, addrlen);
    if (fd < 0)
    {
        throw std::unique_ptr<SocketException>(new SocketException(errno, this));
    }
    return Socket(fd, is_nonblocking);
}

jelford::Socket::~Socket()
{
    std::cerr << m_socket_descriptor << ": closed" << std::endl;
    if (m_socket_descriptor > 0)
    {
        ::close(m_socket_descriptor);
    }
}

std::vector<unsigned char> jelford::Socket::read(size_t length) const
{
    unsigned char buff[__chunk_size];
    ::memset(&buff, 0, sizeof(buff));
    std::vector<unsigned char> data;
    ssize_t read_length = 0;
    ssize_t remaining = length;
    size_t max_read = sizeof(buff) < static_cast<size_t>(remaining) ? sizeof(buff) : static_cast<size_t>(remaining);
    do
    {
        max_read = sizeof(buff) < static_cast<size_t>(remaining) ? sizeof(buff) : static_cast<size_t>(remaining);
        read_length = ::read(m_socket_descriptor, &buff, max_read);

        if (read_length < 0)
            throw std::unique_ptr<SocketException>(new SocketException(errno, this));

        remaining = remaining - read_length;
        data.insert(data.end(), &buff[0], buff+read_length);
    } while(remaining > 0 && max_read - read_length == 0);

    return data;
}

std::vector<unsigned char> jelford::Socket::read() const
{
    std::vector<unsigned char> data;
    auto buff = read(__chunk_size * 10);
    while (buff.size() >= __chunk_size)
    {
        data.insert(data.end(), buff.begin(), buff.end());
        buff = read(__chunk_size);
    }
    data.insert(data.end(), buff.begin(), buff.end());
    return data;
}

void jelford::Socket::write(const std::vector<unsigned char>&& data) const
{
    auto tmp = data;
    tmp.push_back('\0');

    if (::write(m_socket_descriptor, &data[0], data.size()) < 0)
    {
        throw std::unique_ptr<SocketException>(new SocketException(errno, this));
    }
}

void jelford::Socket::write(const std::vector<unsigned char>& data) const
{
    write(std::move(data));
}

void jelford::Socket::connect(sockaddr_in sock_addr)
{
    if (::connect(m_socket_descriptor, reinterpret_cast<sockaddr*>(&sock_addr), sizeof(sock_addr)) < 0)
    {
        if (errno != EINPROGRESS)
            throw std::unique_ptr<SocketException>(new SocketException(errno, this));
    }
}

int jelford::_select_for_reading(int max_fd, fd_set& file_descriptors)
{
    return ::select(max_fd+1, &file_descriptors, NULL, NULL, NULL);
}

int jelford::_select_for_writing(int max_fd, fd_set& file_descriptors)
{
    return ::select(max_fd+1, NULL, &file_descriptors, NULL, NULL);
}

std::vector<const jelford::Socket*> jelford::_select_for(std::vector<const Socket*>& sockets, std::function<int(int,fd_set&)> selector)
{
    fd_set rdfds;
    FD_ZERO(&rdfds);
    int max_fd = -1;
    for (auto s : sockets)
    {
        int fd = s->identify();
        FD_SET(fd, &rdfds);
        max_fd = fd > max_fd ? fd : max_fd;
    }
    int rv = selector(max_fd, rdfds);

    std::vector<const Socket*> ready_sockets;
    
    if (rv > 0)
    {
        for (auto s : sockets)
        {
            if (FD_ISSET(s->identify(), &rdfds))
            {
                ready_sockets.push_back(s);
            }
        }
    }
    else if (rv == 1)
    {
        /* select timed out */
        throw std::unique_ptr<SocketTimeoutException>(new SocketTimeoutException(NULL));
    }
    else
    {
        throw std::unique_ptr<SocketException>(new SocketException(errno, NULL));
    }

    return ready_sockets;

}

const std::vector<const jelford::Socket*> jelford::select_for_reading(std::vector<const Socket*>& sockets)
{
    return _select_for(sockets, _select_for_reading);
}

const std::vector<const jelford::Socket*> jelford::select_for_writing(std::vector<const Socket*>& sockets)
{
    return _select_for(sockets, _select_for_writing);
}

void jelford::wait_for_read(const Socket* socket)
{
    std::vector<const Socket*> s_vector;
    s_vector.push_back(socket);
    auto tmp = select_for_reading(s_vector);
    if (tmp.size() != 1 || std::find(tmp.begin(), tmp.end(), socket) == tmp.end())
        throw std::exception();
}

void jelford::wait_for_write(const Socket* socket) 
{
    std::vector<const Socket*> s_vector;
    s_vector.push_back(socket);
    auto tmp = select_for_writing(s_vector);
    if (tmp.size() != 1 || std::find(tmp.begin(), tmp.end(), socket) == tmp.end())
        throw std::exception();
}

void jelford::Socket::set_nonblocking(bool yes_or_no)
{
    int flags;
    if (-1 == (flags = fcntl(m_socket_descriptor, F_GETFL, 0)))
        flags = 0;
    fcntl(m_socket_descriptor, F_SETFL, flags | O_NONBLOCK);
    is_nonblocking = true;
}



