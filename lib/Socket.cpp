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
#include <sys/ioctl.h>   // ::ioctl
#include <signal.h>     

#include "Socket.hpp"

jelford::SocketException::SocketException(
    const int _errno, const jelford::Socket* socket, std::string where) :  
        _errno(_errno), m_socket(socket), where(where) { }

const char* jelford::SocketException::what()
{
    std::stringstream msg;
    msg << jelford::socket_get_error(_errno);
    msg << " (Errno: " << _errno << ") in " << where;
    return msg.str().c_str();
}

const jelford::Socket* jelford::SocketException::retrieve_socket() const
{
    return m_socket;
}

jelford::SocketTimeoutException::SocketTimeoutException(Socket* s) : SocketException::SocketException(0, s, "timeout error")
{ }

const char* jelford::SocketTimeoutException::what()
{
    return "Timeout occurred on socket operation";
}

const char* jelford::address_get_error(int err)
{
    switch(err)
    {
        //These are all directly from linux.die.net/man/.../getaddrinfo
        case EAI_ADDRFAMILY:
            return "EAI_ADDRFAMILY: Specified network host does not have any addresses in the requested family";
        case EAI_AGAIN:
            return "EAI_AGAIN: Temporary nameserver failure; try again later";
        case EAI_BADFLAGS:
            return "EAI_BADFLAGS: Permanent nameserver error";
        case EAI_FAMILY:
            return "EAI_FAMILY: The requested address family is not supported";
        case EAI_NODATA:
            return "EAI_NODATA: The host exists, but does not have any network addresses defined";
        case EAI_NONAME:
            return "EAI_NONAME: The node or service is not known (wrong hostname?)";
        case EAI_SERVICE:
            return "EAI_SERVICE: Requested service is not available for the requested socket type";
        case EAI_SOCKTYPE:
            return "EAI_SOCKTYPE: Requested socket type is not supported";
        case EAI_SYSTEM:
            return "EAI_SYSTEM: System error (see errno)";
        case ECONNRESET:
            return "ECONNRESET: Connection reset by peer";
        default:
            return "Unknown address error";
    }
}

jelford::AddressException::AddressException(int error, int _errno) : _err(error), _errno(_errno)
{
}

const std::string jelford::AddressException::msg() const
{
    std::stringstream msg;
    msg << address_get_error(_err);
    msg << " [Errno: " << _errno << "]";
    return msg.str();
}

const char* jelford::socket_get_error(int _err)
{
    switch(_err) 
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
        case EAFNOSUPPORT:
            return "EAFNOSUPPORT: Address family not supported by protocol";
        case EADDRNOTAVAIL:
            return "EADDRNOTAVAIL: Cannot assign requested address";
        default:
            return "Unknown socket error";
    }
}

jelford::Address::Address(std::string hostname, std::string port, const addrinfo& hints)
    : address()
{
    addrinfo* result;
    int error;
    if ((error = ::getaddrinfo(hostname.c_str(), port.c_str(), &hints, &result)) < 0)
    {
        throw AddressException(error, errno);
    }
    
    address.reset(result->ai_addr);
    address_length = result->ai_addrlen;
    protocol = result->ai_protocol;
    family = result->ai_family;

    // There's actually much more data in here, which we don't currently
    // use (it's a linked list of different alternatives)
    _addrinfo = result;
}

jelford::Address::Address(std::unique_ptr<sockaddr>&& address, socklen_t address_length, int protocol, int family)
    : _addrinfo(NULL), address(std::move(address)), address_length(address_length), protocol(protocol), family(family)
{
}

jelford::Address::~Address()
{
    if (_addrinfo != NULL)
    {
        // This will get cleaned up by the freeaddrinfo call below
        address.release();
        ::freeaddrinfo(_addrinfo);
    }
}

std::string jelford::Address::family_string() const
{
    switch(family)
    {
        case PF_INET:
            return "PF_INET";
        case PF_INET6:
            return "PF_INET6";
        case PF_UNSPEC:
            return "PF_UNSPEC";
        default:
            return "UNKNOWN";
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
        throw std::unique_ptr<SocketException>(new SocketException(errno, this, "constructor"));
    }
    std::cerr << m_socket_descriptor << ": initialized explicitly" << std::endl;
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

    std::cerr << m_socket_descriptor << ": initialized from file descriptor" << std::endl;
}

jelford::Socket::Socket(Socket&& other) : 
    m_socket_descriptor(other.m_socket_descriptor), is_nonblocking(other.is_nonblocking)
{
    // Don't close it when the old Socket is de-allocated
    other.m_socket_descriptor = -1;

    std::cerr << m_socket_descriptor << ": initialized via move" << std::endl;
}

jelford::Socket& jelford::Socket::operator=(Socket&& other)
{
    this->~Socket();

    this->m_socket_descriptor = other.m_socket_descriptor;
    this->is_nonblocking = other.is_nonblocking;
    other.m_socket_descriptor = -1;
    return *this;

    std::cerr << m_socket_descriptor << ": assigned via move" << std::endl;
}

void jelford::Socket::set_reuse(bool should_reuse)
{
    int optvalue = should_reuse;
    ::setsockopt(m_socket_descriptor, SOL_SOCKET, SO_REUSEADDR, &optvalue, sizeof(optvalue));
}

void jelford::Socket::bind_to(sockaddr* socket_address, socklen_t socket_address_size)
{
    if (::bind(m_socket_descriptor, 
                    socket_address, socket_address_size) < 0)
        throw std::unique_ptr<SocketException>(new SocketException(errno, this, "bind_to"));
}

void jelford::Socket::bind_to(jelford::Address& address)
{
    bind_to(address.address.get(), address.address_length);
}

void jelford::Socket::bind_to(sockaddr_in& socket_address)
{
    bind_to(reinterpret_cast<sockaddr*>(&socket_address), sizeof(socket_address));
}

void jelford::Socket::bind_to(sockaddr_in6& socket_address)
{
    bind_to(reinterpret_cast<sockaddr*>(&socket_address), sizeof(socket_address));
}

void jelford::Socket::listen(int backlog_size)
{
    if (::listen(m_socket_descriptor, backlog_size) < 0)
        throw std::unique_ptr<SocketException>(new SocketException(errno, this, "listen"));
}

jelford::Socket jelford::Socket::accept(sockaddr* addr, socklen_t* addrlen)
{
    auto fd = ::accept(m_socket_descriptor, addr, addrlen);
    if (fd < 0)
    {
        throw std::unique_ptr<SocketException>(new SocketException(errno, this, "accept"));
    }
    return Socket(fd, is_nonblocking);
}

jelford::Socket::~Socket()
{
    if (m_socket_descriptor > 0)
    {
        ::close(m_socket_descriptor);
    }

    std::cerr << m_socket_descriptor << ": deleted" << std::endl;
}

std::vector<unsigned char> jelford::Socket::read(size_t length) const
{
    unsigned char buff[__chunk_size];
    ::memset(&buff, 0, sizeof(buff));
    std::vector<unsigned char> data;
    ssize_t read_length = 0;
    ssize_t remaining = length;
    size_t max_read;

    std::stringstream err_msg;
    int i=1;
    do
    {
        max_read = sizeof(buff) < static_cast<size_t>(remaining) ? sizeof(buff) : static_cast<size_t>(remaining);

        read_length = ::read(m_socket_descriptor, &buff, max_read);

        if (read_length < 0)
        {
            err_msg << "read(length=" << length << ", iteration=" << i << ")";
            throw std::unique_ptr<SocketException>(new SocketException(errno, this, err_msg.str()));
        }

        remaining = remaining - read_length;
        data.insert(data.end(), &buff[0], buff+read_length);
        ++i;
    } while(remaining > 0 && max_read - read_length == 0);

    return data;
}

std::vector<unsigned char> jelford::Socket::read() const
{
    int available = -1;
    if (::ioctl(m_socket_descriptor, FIONREAD, &available) < 0)
        throw std::unique_ptr<SocketException>(new SocketException(errno, this, "read"));

    return read(static_cast<ssize_t>(available));
}

void jelford::Socket::write(const std::vector<unsigned char>&& data) const
{
    if (::write(m_socket_descriptor, &data[0], data.size()) < 0)
    {
        throw std::unique_ptr<SocketException>(new SocketException(errno, this, "write"));
    }
}

void jelford::Socket::write(const std::vector<unsigned char>& data) const
{
    write(std::move(data));
}

void jelford::Socket::connect(sockaddr* sock_addr, socklen_t socket_address_size)
{
    if (::connect(m_socket_descriptor, sock_addr, socket_address_size) < 0)
    {
        if (errno != EINPROGRESS)
            throw std::unique_ptr<SocketException>(new SocketException(errno, this, "connect"));
    }
}

void jelford::Socket::connect(jelford::Address& address)
{
    connect(address.address.get(), address.address_length);
}

void jelford::Socket::connect(sockaddr_in& sock_addr)
{
    connect(reinterpret_cast<sockaddr*>(&sock_addr), sizeof(sock_addr));
}

void jelford::Socket::connect(sockaddr_in6& sock_addr)
{
    connect(reinterpret_cast<sockaddr*>(&sock_addr), sizeof(sock_addr));
}


int jelford::_select_for_reading(int max_fd, fd_set& file_descriptors)
{
    return ::select(max_fd+1, &file_descriptors, NULL, NULL, NULL);
}

int jelford::_select_for_writing(int max_fd, fd_set& file_descriptors)
{
    return ::select(max_fd+1, NULL, &file_descriptors, NULL, NULL);
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



