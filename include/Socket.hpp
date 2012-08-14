#ifndef JELFORD_SOCKET_HPP
#define JELFORD_SOCKET_HPP

#include <vector>       // Use as basic datatype in place of arrays
#include <exception>    // Because we have grown-up error handling 
#include <functional>     // std::function
#include <tuple>

#include <iostream>

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

            void write(const std::vector<unsigned char>& data) const;
    };

    void wait_for_read(const Socket* socket);
    void wait_for_write(const Socket* socket);

    template <typename SocketCollection>
    std::tuple<SocketCollection, SocketCollection, SocketCollection> select(SocketCollection& read_group, SocketCollection& write_group, SocketCollection& exception_group)
    {
        fd_set rdfds, wrfds, exfds;
        FD_ZERO(&rdfds);
        FD_ZERO(&wrfds);
        FD_ZERO(&exfds);
        int max_fd = -1;

        for (auto pair : { std::make_tuple(read_group, &rdfds), std::make_tuple(write_group, &wrfds), std::make_tuple(exception_group, &exfds)})
        {
            for (auto s : std::get<0>(pair))
            {
                int fd = s->identify();
                FD_SET(fd, std::get<1>(pair));
                max_fd = fd > max_fd ? fd : max_fd;
            }
        }

        int rv = ::select(max_fd + 1, (read_group.size() > 0 ? &rdfds : NULL), (write_group.size() > 0 ? &wrfds : NULL), (exception_group.size() > 0 ? &exfds : NULL), NULL);

        SocketCollection read_ready, write_ready, exception_ready;

        if (rv > 0)
        {
            for (auto s : read_group)
            {
                if (FD_ISSET(s->identify(), &rdfds))
                {
                    read_ready.insert(read_ready.end(), s);
                }
            }
            for (auto s : write_group)
            {
                if (FD_ISSET(s->identify(), &wrfds))
                {
                    write_ready.insert(write_ready.end(), s);
                }
            }
            for (auto s : exception_group)
            {
                if (FD_ISSET(s->identify(), &exfds))
                {
                    exception_ready.insert(exception_ready.end(), s);
                }
            }
        } 
        else if (rv == 1)
        {
            throw std::unique_ptr<SocketTimeoutException>(new SocketTimeoutException(NULL));
        } 
        else
        {
            throw std::unique_ptr<SocketException>(new SocketException(errno, NULL, "select"));
        }

        return std::make_tuple(read_ready, write_ready, exception_ready);
    }
}

#endif
