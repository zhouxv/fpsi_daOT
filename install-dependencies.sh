#! /bin/bash

set -e

install_catch2_dependency() {
    CATCH2_INSTALL_PATH=$1

    rm -rf catch2-install-tmp

    printf "##### Cloning catch2 repository #######\n\n"
    git clone https://github.com/catchorg/Catch2.git --branch v3.7.1 catch2-install-tmp 
    cd catch2-install-tmp

    printf "##### Building catch2 #######\n\n"
    cmake -B build -H. -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="../$CATCH2_INSTALL_PATH"

    printf "\n###### Installing catch2 #######\n\n"
    cmake --build build --target install

    cd ..

    # rm -rf catch2-install-tmp
}

install_volepsi_dependency() {
    VOLEPSI_INSTALL_PATH=$1

    printf "####### Updating and upgrading system #######\n\n"
    sudo apt update
    # sudo apt upgrade -y

    printf "\nInstalling system dependencies: nano, python3, software-properties-common, cmake, git, build-essential, libssl-dev, gdb, libtool\n\n"
    sudo apt install -y nano python3 software-properties-common cmake git build-essential libssl-dev gdb libtool
    sudo apt update

    rm -rf volepsi-tmp

    printf "\n###### Cloning volepsi repository #######\n\n"
    git clone https://github.com/Visa-Research/volepsi.git --branch main volepsi-tmp
    
    cd volepsi-tmp

    git reset --hard 00ebece9881913cf281b5eaf74c2a76ec028d37a

    mkdir -p ./out && cd ./out
    wget https://sourceforge.net/projects/boost/files/boost/1.86.0/boost_1_86_0.tar.bz2
    cd ..

    mkdir "../${VOLEPSI_INSTALL_PATH}"

    printf "\n###### Building volepsi #######\n\n"
    python3 build.py -DVOLE_PSI_ENABLE_BOOST=true -DVOLE_PSI_NO_SYSTEM_PATH=true -DCMAKE_BUILD_TYPE=Release -DFETCH_AUTO=true -DFETCH_SPARSEHASH=true -DFETCH_LIBOTE=true
    
    printf "\n###### Installing volepsi #######\n\n"
    python3 build.py --install="../${VOLEPSI_INSTALL_PATH}" -DVOLE_PSI_ENABLE_BOOST=true -DVOLE_PSI_NO_SYSTEM_PATH=true -DCMAKE_BUILD_TYPE=Release -DFETCH_AUTO=true -DFETCH_SPARSEHASH=true -DFETCH_LIBOTE=true

    cd ..

    # rm -rf volepsi-tmp
}

install_catch2_dependency "./catch2"
install_volepsi_dependency "./volepsi"
