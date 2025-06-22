# Proxy DNS Server
This is a minimalistic configurable proxy DNS server.

# Build Instructions (Linux)
To build, run the following commands in bash or sh:
```sh
git clone https://github.com/ksldmitriy/dns-proxy.git
cd dns-proxy
./build.sh
```

# Configuring 
Configuration is done inside the config.toml file. Example configuration is provided in the repository. It has the following fields:

- `dns_server`: IP address of upstream DNS server.
- `blacklist`: An array of blacklisted domain names.
- `refuse_r_code`: RCODE in range from 1 to 5 that will be returned in case of client trying to get the IP of blacklisted domain.
   
# Running and Testing 
## Launching the Server
To run, simply run the `run.sh` script. 
> **_NOTE:_** To bind port 53, you have to run the program with root privileges.

## Testing
To test the server, use the following command:
```sh
dig @localhost -p 53 <domain of choice>
```
