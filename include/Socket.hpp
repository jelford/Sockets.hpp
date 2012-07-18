#ifndef JELFORD_SOCKET_HPP
#define JELFORD_SOCKET_HPP

#include <vector>       // Use as basic datatype in place of arrays
#include <exception>    // Because we have grown-up error handling 
#include <functional>     // std::function

#include <sys/select.h>

#include <netinet/in.h>
#include <netdb.h>  // addrinfo

#include <string>

#include <memory>       // std::shared_ptr

namespace jelford 
{
    class Socket;
    
    class SocketException : public std::exception
    {   
        private:
            const int _errno;
            const jelford::Socket* m_socket;
            std::string where;
        public:
            SocketException(const int _errno, const Socket* socket, std::string where);
            
            virtual const char* what();
            const jelford::Socket* retrieve_socket() const;
    };

    class SocketTimeoutException: public SocketException
    {
        public:
            SocketTimeoutException(Socket*);

            virtual const char* what();
    };

    const char* address_get_error(int _errno);

    class AddressException : public std::exception
    {
        private:
            int _err;
            int _errno;
        public:
            AddressException(int err, int _errno);
            virtual const std::string msg() const;
    };


    const char* socket_get_error(int _errno);

    class Address
    {
        private:
            addrinfo* _addrinfo;
        public:
            Address(std::string host, std::string port, const addrinfo& hints); 
            Address(std::unique_ptr<sockaddr>&&, socklen_t, int, int);
            virtual ~Address();

            std::unique_ptr<sockaddr> address;
            socklen_t address_length;
            int protocol;
            int family;
            
            std::string family_string() const;
    };

    // Requires sys/socket.h
    class Socket
    {
        private:
            static const size_t __chunk_size = 65536;
            int m_socket_descriptor;
            bool is_nonblocking;
            Socket(Socket&) = delete;
        public:
            Socket(int socket_family, int socket_type, int protocol) throw(SocketException);
            Socket(int file_descriptor, bool is_nonblocking);
            Socket(Socket&& other);
            Socket& operator=(Socket&& other);
            virtual ~Socket();
    
            int identify() const;
            bool is_listening() const;
            bool other_end_has_hung_up() const;

            void set_nonblocking(bool);
            void set_reuse(bool should_reuse);
            
            void listen(int backlogsize=32);
            Socket accept(sockaddr* addr, socklen_t* addrlen);

            void bind_to(Address& socket_address);
            void bind_to(sockaddr* socket_address, socklen_t socket_address_size);
            void bind_to(sockaddr_in& socket_address);
            void bind_to(sockaddr_in6& socket_address);

            void connect(Address& socket_address);
            void connect(sockaddr* socket_address, socklen_t socket_address_size);
            void connect(sockaddr_in& socket_address);
            void connect(sockaddr_in6& socket_address);
            
            std::vector<unsigned char> read(size_t length) const;
            std::vector<unsigned char> read() const;

            void write(const std::vector<unsigned char>&& data) const;
            void write(const std::vector<unsigned char>& data) const;
    };

    void wait_for_read(const Socket* socket);
    void wait_for_write(const Socket* socket);

    int _select_for_reading(int, fd_set&);
    int _select_for_writing(int, fd_set&);

    template <typename SocketCollection>
    SocketCollection _select_for(SocketCollection& sockets, std::function<int(int, fd_set&)> selector)
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

        SocketCollection ready_sockets;
        
        if (rv > 0)
        {
            for (auto s : sockets)
            {
                if (FD_ISSET(s->identify(), &rdfds))
                {
                    ready_sockets.insert(ready_sockets.end(), s);
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
            throw std::unique_ptr<SocketException>(new SocketException(errno, NULL, "select"));
        }

        return ready_sockets;

    }


    template <typename SocketCollection>
    const SocketCollection select_for_reading(SocketCollection& sockets)
    {
        return _select_for(sockets, _select_for_reading);
    }

    template <typename SocketCollection>
    const SocketCollection select_for_writing(SocketCollection& sockets)
    {
        return _select_for(sockets, _select_for_writing);
    }

}

#endif
