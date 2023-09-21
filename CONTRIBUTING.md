## Pre-requisites
1. sender-slink2dali is dependent on [earthquake-hub-backend](https://github.com/UPRI-earthquake/earthquake-hub-backend) as its AuthServer. Make sure to setup and run this on your system.
2. It is also dependent on [ringserver](https://github.com/UPRI-earthquake/receiver-ringserver) as the data destination. Make sure to also setup and run this on your system.
3. An accessible SeedLink server is also required as the source of data. You may use `geofon.gfz-potsdam.de:18000` as an online source. Or if you have a Raspberry Shake, you may locally connect to its SeedLink server on `rs.local:18000`
4. Make sure you have build tools installed. This depends on the system you're using. We're using Ubuntu linux in our dev env. 
    ```bash
    sudo apt install gcc make
    ```

## Setting Up The Repository On Your Local Machine
1. Clone the repository:
    ```bash
    git clone git@github.com:UPRI-earthquake/sender-slink2dali.git
    ```
2. Build the source code with a simple `make` command:
    ```bash
    make
    ```
3. Acquire *sensor token* from AuthServer, make sure that earthquake-hub-backend is running and that you have previously created this account. Send a POST request to `http://172.22.0.3:5000/accounts/authenticate` with the following request body:
    ```json
    {
        "username": "citizen",
        "password": "testpassword",
        "role": "sensor"
    }
    ```
4. Save the `auth token` to a variable in the terminal:
    ```bash
    TOKEN=<auth-token-received-from-AuthServer>
    ```
5. Execute slink2dali via:
    ```bash
    ./slinkdali -vvv -a $TOKEN -S <net_sta> <seedlink-server> <ringserver-host>
    ```
    - `$TOKEN` - auth token from the AuthServer saved in a variable
    - `net_sta` - device network and station codes, ie `GE_TOLI2` or `AM_<station-code-of-your-rshake>`
    - `seedlink-server` - data source, ie `geofon.gfz-potsdam.de` or `rs.local`
    - `ringserver-host` - IP address of the RingServer you're running, ie `localhost:16000`
