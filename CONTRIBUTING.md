## Pre-requisites
1. sender-slink2dali is dependent on [earthquake-hub-backend](https://github.com/UPRI-earthquake/earthquake-hub-backend) as its AuthServer, to which sensor auth token will be acquired from.
2. To properly run this program, make sure to run [ringserver](https://github.com/UPRI-earthquake/receiver-ringserver), as well.

## Setting Up The Repository On Your Local Machine
1. Clone the repository:
    ```bash
    git clone git@github.com:UPRI-earthquake/sender-slink2dali.git
    ```
2. Build the source code with a simple `make` command:
    ```bash
    make
    ```
3. Acquire *sensor token* fromAuthServer, make sure that earthquake-hub-backend is running to do this. Send a POST request to `http://172.22.0.3:5000/accounts/authenticate` with the following request body:
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
    - `net_sta` - device network and station codes
    - `seedlink-server` - data source
    - `ringserver-host` - server which will receive data packets sent by the seedlink-server
