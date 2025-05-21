# Embarcatech-Residência
## Embarca-Server
#### Autor:
* Roberto Vítor Lima Gomes Rodrigues

### Webserver
Para o desenvolvimento do Webserver, foram utilizados o terminal wi-fi cyw43-arch, os LEDs RGB, a matriz de LEDs WS2818 e os Buzzers.

#### Vídeo de funcionamento
* https://youtu.be/JTe9A3n6bv4


#### Instruções de compilação
Certifique-se de que você tem o ambiente configurado conforme abaixo:
* Pico SDK instalado e configurado.
* VSCode com todas as extensões configuradas, CMake e Make instalados.
* Clone o repositório e abra a pasta do projeto, a extensão Pi Pico criará a pasta build
* Altere as linhas referentes à conexão Wi-Fi
* Clique em Compile na barra inferior, do lado direito (ao lado esquerdo de RUN | PICO SDK)
* Verifique se gerou o arquivo .uf2
* Conecte a placa BitDogLab e ponha-a em modo BOOTSEL
* Arraste o arquivo até a placa, que o programa se iniciará

#### Manual do programa
###### Antes de executar o programa, é preciso alterar as linhas referentes à conexão Wi-Fi
Ao executar o programa, ele buscará conectar-se com a rede informada:
   * É importante abrir o monitor serial
* O LED RGB azul ficará acesso até que ele se conecte;
* Se o LED RGB vermelho se acender, quer dizer que ele não conseguiu conectar-se, aperte RESET para tentar novamente (e verifique suas informações de conexão)
* Quando o LED RGB verde se acender, quer dizer que a conexão foi bem sucedida
    * Na hora da conexão, aparecerá o ip respectivo no monitor serial
    * Digite o ip no navegador, e será aberta a página do webserver
    * Será possível ver na página um painel de interação com os periféricos da residência automatizada 
    * Os quatro primeiros botões alteram os níveis de intensidade da matriz de LEDs, como se estivesse acendendo uma luminária
        * Os botões dividem-se em: alta intensidade, média intensidade e baixa intensidade, e um botão para desligar
    * Os botões a seguir fazem referência a:
        * Uma campainha, que toca o buzzer para avisar os moradores da chegada
        * Um botão que simula o controle de uma mangueira de água para o jardim com dois botões: ligado e desligado
    * Ao final, há um medidor de temperatura
