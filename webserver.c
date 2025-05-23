#include <stdio.h>               // Biblioteca padrão para entrada e saída
#include <string.h>              // Biblioteca manipular strings
#include <stdlib.h>              // funções para realizar várias operações, incluindo alocação de memória dinâmica (malloc)

#include "pico/stdlib.h"         // Biblioteca da Raspberry Pi Pico para funções padrão (GPIO, temporização, etc.)
#include "hardware/adc.h"        // Biblioteca da Raspberry Pi Pico para manipulação do conversor ADC
#include "pico/cyw43_arch.h"     // Biblioteca para arquitetura Wi-Fi da Pico com CYW43  
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "RVpio.pio.h"

#include "lwip/pbuf.h"           // Lightweight IP stack - manipulação de buffers de pacotes de rede
#include "lwip/tcp.h"            // Lightweight IP stack - fornece funções e estruturas para trabalhar com o protocolo TCP
#include "lwip/netif.h"          // Lightweight IP stack - fornece funções e estruturas para trabalhar com interfaces de rede (netif)

#include "pico/bootrom.h"

// Credenciais WIFI - Tome cuidado se publicar no github!
#define WIFI_SSID "SSID"
#define WIFI_PASSWORD "SENHA"


#define PWM_WRAP 20000 //contador do PWM
#define PWM_CLKDIV 125.0f //divisor de clock do PWM

// Definição dos pinos dos LEDs
#define LED_PIN CYW43_WL_GPIO_LED_PIN   // GPIO do CI CYW43
#define LED_BLUE_PIN 12                 // GPIO12 - LED azul
#define LED_GREEN_PIN 11                // GPIO11 - LED verde
#define LED_RED_PIN 13                  // GPIO13 - LED vermelho
#define BUTTON_B 6
#define BUZZER_A 10 // BUZZER A
#define BUZZER_B 21 // BUZZER B


//struct para armazenar a pio
typedef struct pio_refs{
    PIO address;
    int state_machine;
    int offset;
    int pin;
} pio_ref;

//struct para armazenar a cor para o desenho
typedef struct rgb{
    double red;
    double green;
    double blue;
} rgb;

//struct para armazenar o desenho
typedef struct drawing {
    double figure[25]; /**< Matriz de dados da figura. */
    rgb main_color;  /**< Cor principal da figura. */
} sketch;

//definição de pio estática para manipulação facilitada através das requisições
static pio_ref my_pio;
// Inicia PWM para os pinos dos LEDs e Buzzer 
void led_pwm(void);
void buzzer_pwm(void);
//Configura a pio
void config_pio(pio_ref *pio);

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err);

// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

// Leitura da temperatura interna
float temp_read(void);

// Tratamento do request do usuário
int user_request(char **request);

void config_pio(pio_ref* pio);

//retorna a cor da matriz de leds
uint32_t rgb_matrix(rgb color);

//desenha na matrix de leds
void draw(sketch sketch, uint32_t led_cfg, pio_ref pio, const uint8_t vector_size);

void reboot(uint gpio, uint32_t events){
    reset_usb_boot(0,0);
}

// Função principal
int main()
{

    gpio_init(BUTTON_B);
    gpio_set_dir(BUTTON_B, GPIO_IN);
    gpio_pull_up(BUTTON_B);
    gpio_set_irq_enabled_with_callback(BUTTON_B, GPIO_IRQ_EDGE_FALL, true, &reboot);
    //Inicializa todos os tipos de bibliotecas stdio padrão presentes que estão ligados ao binário.
    stdio_init_all();

    // Inicia o PWM para os pinos dos LEDs e buzzers
    led_pwm();
    buzzer_pwm();

    //atribui os valores iniciais à pio estática
    my_pio.pin = 7;
    my_pio.address = 0;
    my_pio.offset = 0;
    my_pio.state_machine = 0;

    //configura a pio estática
    config_pio(&my_pio);

    //ativa pwm no led azul para mostrar que está buscando a rede
    pwm_set_gpio_level(LED_BLUE_PIN, 1024);

    //Inicializa a arquitetura do cyw43
    while (cyw43_arch_init())
    {
        pwm_set_gpio_level(LED_GREEN_PIN, 0);
        pwm_set_gpio_level(LED_BLUE_PIN, 0);
        pwm_set_gpio_level(LED_RED_PIN, 1024);
        printf("Falha ao inicializar Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }

    // GPIO do CI CYW43 em nível baixo
    cyw43_arch_gpio_put(LED_PIN, 1);

    // Ativa o Wi-Fi no modo Station, de modo a que possam ser feitas ligações a outros pontos de acesso Wi-Fi.
    cyw43_arch_enable_sta_mode();

    // Conectar à rede WiFI - fazer um loop até que esteja conectado
    printf("Conectando ao Wi-Fi...\n");

    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000))
    {
        pwm_set_gpio_level(LED_RED_PIN, 1024);
        pwm_set_gpio_level(LED_BLUE_PIN, 0);
        printf("Falha ao conectar ao Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }

    printf("Conectado ao Wi-Fi\n");
    pwm_set_gpio_level(LED_BLUE_PIN, 0);
    //ativa o pino verde para indicar que está conectado
    pwm_set_gpio_level(LED_GREEN_PIN, 1024);
    pwm_set_gpio_level(LED_RED_PIN, 0);

    // Caso seja a interface de rede padrão - imprimir o IP do dispositivo.
    if (netif_default)
    {
        printf("IP do dispositivo: %s\n", ipaddr_ntoa(&netif_default->ip_addr));
    }

    // Configura o servidor TCP - cria novos PCBs TCP. É o primeiro passo para estabelecer uma conexão TCP.
    struct tcp_pcb *server = tcp_new();
    if (!server)
    {
        printf("Falha ao criar servidor TCP\n");
        return -1;
    }

    //vincula um PCB (Protocol Control Block) TCP a um endereço IP e porta específicos.
    if (tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Falha ao associar servidor TCP à porta 80\n");
        return -1;
    }

    // Coloca um PCB (Protocol Control Block) TCP em modo de escuta, permitindo que ele aceite conexões de entrada.
    server = tcp_listen(server);

    // Define uma função de callback para aceitar conexões TCP de entrada. É um passo importante na configuração de servidores TCP.
    tcp_accept(server, tcp_server_accept);
    printf("Servidor ouvindo na porta 80\n");

    // Inicializa o conversor ADC
    adc_gpio_init(27);
    adc_gpio_init(26);
    adc_init();
    adc_set_temp_sensor_enabled(true);

    while (true)
    {
        /* 
        * Efetuar o processamento exigido pelo cyw43_driver ou pela stack TCP/IP.
        * Este método deve ser chamado periodicamente a partir do ciclo principal 
        * quando se utiliza um estilo de sondagem pico_cyw43_arch 
        */
        cyw43_arch_poll(); // Necessário para manter o Wi-Fi ativo
        sleep_ms(100);      // Reduz o uso da CPU
    }

    //Desligar a arquitetura CYW43.
    cyw43_arch_deinit();
    return 0;
}

// -------------------------------------- Funções ---------------------------------

// Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
void led_pwm(void){
    // Configuração pwm para o led azul
    gpio_set_function(LED_BLUE_PIN, GPIO_FUNC_PWM);
    int blue_slice = pwm_gpio_to_slice_num(LED_BLUE_PIN);

    pwm_set_wrap(blue_slice, PWM_WRAP);
    pwm_set_clkdiv(blue_slice, PWM_CLKDIV);  
    pwm_set_enabled(blue_slice, true);

    //configura pwm para o led verde
    gpio_set_function(LED_GREEN_PIN, GPIO_FUNC_PWM);
    int green_slice = pwm_gpio_to_slice_num(LED_GREEN_PIN);

    pwm_set_wrap(green_slice, PWM_WRAP);
    pwm_set_clkdiv(green_slice, PWM_CLKDIV);  
    pwm_set_enabled(green_slice, true);

    //configura pwm para o led vermelho
    gpio_set_function(LED_RED_PIN, GPIO_FUNC_PWM);
    int red_slice = pwm_gpio_to_slice_num(LED_RED_PIN);

    pwm_set_wrap(red_slice, PWM_WRAP);
    pwm_set_clkdiv(red_slice, PWM_CLKDIV); 
    pwm_set_enabled(red_slice, true); 
}

void buzzer_pwm(void){
    // Confira pwm para o buzzer a
    gpio_set_function(BUZZER_A, GPIO_FUNC_PWM);
    int a_slice = pwm_gpio_to_slice_num(BUZZER_A);

    pwm_set_wrap(a_slice, PWM_WRAP);
    pwm_set_clkdiv(a_slice, PWM_CLKDIV);  
    pwm_set_enabled(a_slice, true);

    //configura pwm para o buzzer b
    gpio_set_function(BUZZER_B, GPIO_FUNC_PWM);
    int b_slice = pwm_gpio_to_slice_num(BUZZER_B);

    pwm_set_wrap(b_slice, PWM_WRAP);
    pwm_set_clkdiv(b_slice, PWM_CLKDIV);  
    pwm_set_enabled(b_slice, true); 
}

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

// Tratamento do request do usuário - digite aqui
int user_request(char **request){
 
    //acabou que não utilizei esses inteiros
    int level = 0;
    int pwm_level = 0;

    //buffer auxiliar para não acabar corrompendo a requisição
    char aux[strlen(*request)];
    strcpy(aux, *request);

    //acende a luminária na maior intensidade
    if (strstr(aux, "GET /led_h") != NULL)
    {
        sketch sketch = {
            .main_color = {
                .blue = 0.1 , .green = 0.1, .red = 0.1
            },
            .figure = {
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1
            } 
        };
        draw(sketch, 0, my_pio, 25);
        //printf("\n\n\nMATRIZ: %d\n\n\n", level);
    } else if (strstr(aux, "GET /led_m") != NULL) // acende a luminária na intensidade média
    {
        sketch sketch = {
            .main_color = {
                .blue = 0.05, .green = 0.05, .red = 0.05
            },
            .figure = {
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1
            } 
        };
        draw(sketch, 0, my_pio, 25);
        //printf("\n\n\nMATRIZ: %d\n\n\n", level);
    } else if (strstr(aux, "GET /led_l") != NULL) //acende a luminária na baixa intensidade
    {
        sketch sketch = {
            .main_color = {
                .blue = 0.01, .green = 0.01, .red = 0.01
            },
            .figure = {
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1
            } 
        };
        draw(sketch, 0, my_pio, 25);
        //printf("\n\n\nMATRIZ: %d\n\n\n", level);
    } else if (strstr(aux, "GET /led_o") != NULL) //desliga a luminária
    {
        sketch sketch = {
            .main_color = {
                .blue = 0.0, .green = 0.0, .red = 0.0
            },
            .figure = {
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1
            } 
        };
        draw(sketch, 0, my_pio, 25);
        //printf("\n\n\nMATRIZ: %d\n\n\n", level);
    }
    if (strstr(aux, "GET /buzzer") != NULL) //liga o buzzer
    {
        pwm_set_gpio_level(BUZZER_A, 0.5 * PWM_WRAP);
        pwm_set_gpio_level(BUZZER_B, 0.5 * PWM_WRAP);
        sleep_ms(500);
        pwm_set_gpio_level(BUZZER_A, 0);
        pwm_set_gpio_level(BUZZER_B, 0);
    }
    if (strstr(aux, "GET /water_h") != NULL) //ativa o acionamento de água
    {
        sketch sketch = {
            .main_color = {
                .blue = 0.05 , .green = 0.01 , .red = 0.01 
            },
            .figure = {
                0, 1, 1, 1, 0,
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1,
                0, 1, 1, 1, 0,
                0, 0, 1, 0, 0
            } 
        };
        draw(sketch, 0, my_pio, 25);
        pwm_level = 1;
        //printf("\n\n\nMATRIZ: %d\n\n\n", level);
    } else if (strstr(aux, "GET /water_o") != NULL) //desliga o acionamento de água
    {
        sketch sketch = {
            .main_color = {
                .blue = 0.0, .green = 0.0, .red = 0.0
            },
            .figure = {
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1,
                1, 1, 1, 1, 1
            } 
        };
        draw(sketch, 0, my_pio, 25);
        pwm_level = -1;
        //printf("\n\n\nMATRIZ: %d\n\n\n", level);
    }

    return pwm_level;
};

// Leitura da temperatura interna
float temp_read(void){
    adc_select_input(4);
    uint16_t raw_value = adc_read();
    const float conversion_factor = 3.3f / (1 << 12);
    float temperature = 27.0f - ((raw_value * conversion_factor) - 0.706f) / 0.001721f;
        return temperature;
}


static int current_pwm_level = 0;
static int lastLevel = 0;
// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    // Alocação do request na memória dinámica
    char *request = (char *)malloc(p->len + 1);
    memcpy(request, p->payload, p->len);
    request[p->len] = '\0';

    printf("Request: %s\n", request);

    // Tratamento de request - Controle dos LEDs
    
    int level = user_request(&request);
    if (level != 0)
        lastLevel = level;

    // Leitura da temperatura interna
    //float temperature = temp_read();
    adc_select_input(1); // GPIO 26 = ADC0
    float temperature = (adc_read() * 50) / 4095;
    adc_select_input(0); // GPIO 26 = ADC0
    int humidity = (adc_read() * 100) / 4095;
    if (lastLevel > 0){
        humidity += 5;
        temperature -=1;
    }

    char condition[7];

    if ((humidity > 30 && humidity < 50) && (temperature > 20 && temperature < 30))
        sprintf(condition, "boas!");
    else 
        sprintf(condition, "ruins!");

    // Cria a resposta HTML
    char html[3072];

    // Instruções html do webserver
    snprintf(html, sizeof(html), // Formatar uma string e armazená-la em um buffer de caracteres
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n"
            "\r\n"
            "<!DOCTYPE html>"
            "<html>"
                "<head>"
                    "<title>🏠Painel</title>"
                    "<meta charset=\"UTF-8\">"
                    "<style>"
                        "body{"
                            "background:#f8f9fa;"
                            "font-family:Arial;"
                            "margin:0;"
                            "min-height:100vh;"
                            "display:flex;"
                            "flex-direction:column;"
                            "align-items:center;}"
                        ".container{"
                            "max-width:800px;"
                            "margin:0 auto;"
                            "padding:20px;"
                            "display:flex;"
                            "flex-direction: row;}"
                        ".section{"
                            "display:flex;"
                            "flex-direction:column;"
                            "background:#f6f6f6;"
                            "border-radius:10px;"
                            "padding: 10px;"
                            "box-shadow:0 4px 6px rgba(0,0,0,0.1);"
                            "}"
                        ".card{background:#fff;"
                            "border-radius:10px;"
                            "box-shadow:0 4px 6px rgba(0,0,0,0.1);"
                            "padding:20px;"
                            "margin-bottom:20px;}"
                        ".content{display:flex;"
                            "flex-direction:row;"
                            "flex-wrap:wrap;}"
                        ".btn{"
                            "display:inline-flex;"
                            "align-items:center;"
                            "justify-content:center;"
                            "background:#6c757d;"
                            "color:white;"
                            "border:none;"
                            "border-radius:5px;"
                            "padding:12px 24px;"
                            "font-size:18px;"
                            "margin:8px;"
                            "cursor:pointer;"
                            "transition:all 0.3s;}"
                        ".btn:hover{"
                            "opacity:0.8;"
                            "transform:translateY(-2px);}"
                        ".btn-p{"
                            "background:#0d6efd;}"
                        ".btn-d{"
                            "background:#dc3545;}"
                        ".btn-s{"
                            "background:#198754;}"
                        ".btn-w{"
                            "background:#ffc107;color:#000;}"
                        ".form-group{"
                            "margin-bottom:1rem;"
                            "display:flex;"
                            "align-items:center;}"
                        "select{"
                            "padding:8px;"
                            "border-radius:4px;"
                            "border:1px solid #ced4da;"
                            "margin-left:10px;}"
                        "h1{"
                            "color:#212529;"
                            "margin-bottom:1.5rem;}"
                        ".sensor{"
                            "font-size:12px;"
                            "color:#495057;"
                            "margin-top:1rem;"
                            "flex:1 1 50%%}"
                        ".text{"
                            "display:flex;"
                            "flex:1 1 100%%;}"
                    "</style>"
                    "<script>"
                    "setTimeout(()=>{"
                        "location.reload();"
                    "},10000);" // 30000 ms = 30 segundos
                    "</script>"
                "</head>"
                "<body>"
                    "<h1>🏠 Painel</h1>"
                    "<div class=\"container\">"
                        "<div class=\"office section\">"
                            "<h4>Escritório</h4>"
                            "<div class=\"card\">"
                                "<h4>💡 Luz</h4>"
                                "<div class=\"content\">"
                                    "<form action=\"./led_h\" method=\"GET\" class=\"form-group\">"
                                        "<button type=\"submit\" class=\"btn btn-p\">Alto</button>"
                                    "</form>"
                                    "<form action=\"./led_m\" method=\"GET\" class=\"form-group\">"
                                        "<button type=\"submit\" class=\"btn btn-p\">Médio</button>"
                                    "</form>"
                                    "<form action=\"./led_l\" method=\"GET\" class=\"form-group\">"
                                        "<button type=\"submit\" class=\"btn btn-p\">Baixo</button>"
                                    "</form>"
                                    "<form action=\"./led_o\" method=\"GET\" class=\"form-group\">"
                                        "<button type=\"submit\" class=\"btn btn-d\">Off</button>"
                                    "</form>"
                                "</div>"
                            "</div>"
                            "<div class=\"card\">"
                                "<h4>Outros</h4>"
                                "<div class=\"content\">"
                                    "<form action=\"./buzzer\" method=\"GET\">"
                                        "<button type=\"submit\" class=\"btn btn-w\">🔔 Toque</button>"
                                    "</form>"
                                "</div>"
                            "</div>"
                        "</div>"
                        "<div class=\"greenhouse section\">"
                            "<h4>Estufa</h4>"
                            "<div class=\"card\">"
                                "<h4>Sensores</h4>"
                                "<div class=\"content\">"
                                    "<p class=\"sensor temp\">🌡️ Temperatura: %.2f°C</p>"
                                    "<p class=\"sensor moisture\">💧 Umidade: %d%%</p>"
                                    "<p class=\"text\">As condições estão %s</p>"
                                "</div>"
                            "</div>"
                            "<div class=\"card\">"
                                "<h4>Atuadores</h4>"
                                "<div class=\"content\">"
                                    "<form action=\"./water_h\" method=\"GET\">"
                                        "<button type=\"submit\" class=\"btn btn-s\">🚿 Água</button>"
                                    "</form>"
                                    "<form action=\"./water_o\" method=\"GET\">"
                                        "<button type=\"submit\" class=\"btn btn-s\">🚱 Água</button>"
                                    "</form>"
                                "</div>"
                        "</div>"
                    "</div>"
                "</body>"
            "</html>", temperature, humidity, condition);

    // Escreve dados para envio (mas não os envia imediatamente).
    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);

    // Envia a mensagem
    tcp_output(tpcb);

    //libera memória alocada dinamicamente
    free(request);
    
    //libera um buffer de pacote (pbuf) que foi alocado anteriormente
    pbuf_free(p);

    return ERR_OK;
}

void config_pio(pio_ref* pio){
    pio->address = pio0;
    if (!set_sys_clock_khz(128000, false))
        printf("clock errado!");
    pio->offset = pio_add_program(pio->address, &pio_review_program);
    pio->state_machine = pio_claim_unused_sm(pio->address, true);

    pio_review_program_init(pio->address, pio->state_machine, pio->offset, pio->pin);
}

uint32_t rgb_matrix(rgb color){
    unsigned char r, g, b;
    r = color.red* 255;
    g = color.green * 255;
    b = color.blue * 255;
    return (g << 24) | (r << 16) | (b << 8);
}

void draw(sketch sketch, uint32_t led_cfg, pio_ref pio, const uint8_t vector_size){
    for(int16_t i = 0; i < vector_size; i++){
        if (sketch.figure[i] == 1)
            led_cfg = rgb_matrix(sketch.main_color);
        else
            led_cfg = 0;
        pio_sm_put_blocking(pio.address, pio.state_machine, led_cfg);
    }
};

