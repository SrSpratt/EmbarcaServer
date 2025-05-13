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

// Credenciais WIFI - Tome cuidado se publicar no github!
#define WIFI_SSID "SSID"
#define WIFI_PASSWORD "SENHA"

#define PWM_WRAP 20000
#define PWM_CLKDIV 125.0f

// Definição dos pinos dos LEDs
#define LED_PIN CYW43_WL_GPIO_LED_PIN   // GPIO do CI CYW43
#define LED_BLUE_PIN 12                 // GPIO12 - LED azul
#define LED_GREEN_PIN 11                // GPIO11 - LED verde
#define LED_RED_PIN 13                  // GPIO13 - LED vermelho
#define BUZZER_A 10
#define BUZZER_B 21

typedef struct pio_refs{
    PIO address;
    int state_machine;
    int offset;
    int pin;
} pio_ref;

typedef struct rgb{
    double red;
    double green;
    double blue;
} rgb;

typedef struct drawing {
    double figure[25]; /**< Matriz de dados da figura. */
    rgb main_color;  /**< Cor principal da figura. */
} sketch;

static pio_ref my_pio;
// Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
void led_pwm(void);
void buzzer_pwm(void);
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

uint32_t rgb_matrix(rgb color);

void draw(sketch sketch, uint32_t led_cfg, pio_ref pio, const uint8_t vector_size);

// Função principal
int main()
{
    //Inicializa todos os tipos de bibliotecas stdio padrão presentes que estão ligados ao binário.
    stdio_init_all();

    // Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
    led_pwm();
    buzzer_pwm();

    my_pio.pin = 7;
    my_pio.address = 0;
    my_pio.offset = 0;
    my_pio.state_machine = 0;

    config_pio(&my_pio);

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
    // Configuração dos LEDs como saída
    gpio_set_function(LED_BLUE_PIN, GPIO_FUNC_PWM);
    int blue_slice = pwm_gpio_to_slice_num(LED_BLUE_PIN);

    pwm_set_wrap(blue_slice, PWM_WRAP);
    pwm_set_clkdiv(blue_slice, PWM_CLKDIV);  
    pwm_set_enabled(blue_slice, true);

    gpio_set_function(LED_GREEN_PIN, GPIO_FUNC_PWM);
    int green_slice = pwm_gpio_to_slice_num(LED_GREEN_PIN);

    pwm_set_wrap(green_slice, PWM_WRAP);
    pwm_set_clkdiv(green_slice, PWM_CLKDIV);  
    pwm_set_enabled(green_slice, true);

    gpio_set_function(LED_RED_PIN, GPIO_FUNC_PWM);
    int red_slice = pwm_gpio_to_slice_num(LED_RED_PIN);

    pwm_set_wrap(red_slice, PWM_WRAP);
    pwm_set_clkdiv(red_slice, PWM_CLKDIV); 
    pwm_set_enabled(red_slice, true); 
}

void buzzer_pwm(void){
    // Configuração dos LEDs como saída
    gpio_set_function(BUZZER_A, GPIO_FUNC_PWM);
    int a_slice = pwm_gpio_to_slice_num(BUZZER_A);

    pwm_set_wrap(a_slice, PWM_WRAP);
    pwm_set_clkdiv(a_slice, PWM_CLKDIV);  
    pwm_set_enabled(a_slice, true);

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
 
    int level = 0;
    int pwm_level = 0;

    char aux[strlen(*request)];
    strcpy(aux, *request);

    if (strstr(aux, "level=high") != NULL)
    {
        level = 10;
        pwm_level = 1;
        //printf("\n\nENTROU HIGH\n\n");
    } else if (strstr(aux, "level=so-so") != NULL){
        level = 5;
        pwm_level = 2;
        //printf("\n\nENTROU SO-AND-SO\n\n");
    } else if (strstr(aux, "level=low") != NULL){
        level = 2;
        pwm_level = 3;
        //printf("\n\nENTROU LOW\n\n");
    } else if (strstr(aux, "level=none") != NULL){
        level = 0;
        pwm_level = 4;
        //printf("\n\nENTROU OFF\n\n");
    }

    if (strstr(aux, "GET /lamp") != NULL)
    {
        sketch sketch = {
            .main_color = {
                .blue = 0.01 * level, .green = 0.01 * level, .red = 0.01 * level
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
    } else if (strstr(aux, "GET /buzzer") != NULL)
    {
        pwm_set_gpio_level(BUZZER_A, 0.1 * level * PWM_WRAP);
        pwm_set_gpio_level(BUZZER_B, 0.1 * level * PWM_WRAP);
        sleep_ms(100);
        pwm_set_gpio_level(BUZZER_A, 0);
        pwm_set_gpio_level(BUZZER_B, 0);
    } else if (strstr(aux, "GET /water") != NULL)
    {
        sketch sketch = {
            .main_color = {
                .blue = 0.05 * level, .green = 0.01 * level, .red = 0.01 * level
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
    
    // Leitura da temperatura interna
    float temperature = temp_read();

    // Cria a resposta HTML
    char html[2048];

    char led_high[10];
    char led_soso[10];
    char led_low[10];
    char led_none[10];

    if (level != 0) {
        current_pwm_level = level;
    }
    // printf("\n\n\nPWMLEVEL: %d\n\n", level);
    // printf("\n\n\nCURRENTPWMLEVEL: %d", current_pwm_level);

    strcpy(led_high, current_pwm_level == 1 ? "selected" : " ");
    strcpy(led_soso, current_pwm_level == 2 ? "selected" : " ");
    strcpy(led_low,  current_pwm_level == 3 ? "selected" : " ");
    strcpy(led_none, (current_pwm_level == 4) || (current_pwm_level == 0) ? "selected" : " ");
    // printf("\n\n\nALTO: %s", led_high);
    // printf("\n\n\nMEDIO: %s", led_soso);
    // printf("\n\n\nBAIXO: %s", led_low);
    // printf("\n\n\nDESLIGADO: %s", led_none);

    // Instruções html do webserver
    snprintf(html, sizeof(html), // Formatar uma string e armazená-la em um buffer de caracteres
            // "HTTP/1.1 200 OK\r\n"
            // "Content-Type: text/html\r\n"
            // "Access-Control-Allow-Origin: *\r\n"
            // "Connection: keep-alive\r\n"
            // "Content-length: 2048\r\n"
            // "Connection: close\r\n"
            // "\r\n"
            // "<!DOCTYPE html>"
            // "<html>"
            //     "<head>"
            //         "<title>Home Control</title>"
            //         "<meta charset=\"UTF-8\">"
            //         "<style>\n"
            //             "body {background-color: #b5e5fb; font-family: Arial, sans-serif; text-align: center; margin-top: 50px;}"
            //             "h1 {font-size: 64px; margin-bottom: 30px; }\n"
            //             "button {background-color: LightGray; font-size: 36px; margin: 10px; padding: 20px 40px; border-radius: 10px;}"
            //             ".temperature {font-size: 48px; margin-top: 30px; color: #333;}"
            //             ".container {}"
            //         "</style>"
            //     "</head>"
            //     "<body>"
            //         "<div class=\"container\">"
            //             "<h1 class=\"display-4\">🏠 Controle residencial</h1>"
            //         "</div>"
            //         "<div class=\"container\">"
            //             "<form action=\"./lamp\" method=\"GET\">"
            //                 "<label for=\"priority\">Nível</label>"
            //                 "<select name=\"level\" id=\"priority\" style=\"font-size: 32px;\" required>"
            //                     "<option value=\"high\" %s>Alto</option>"
            //                     "<option value=\"so-so\" %s>Medio</option>"
            //                     "<option value=\"low\" %s>Baixo</option>"
            //                     "<option value=\"none\" %s>Desligar</option>"
            //                 "</select>"
            //                 "<button type=\"submit\">💡 Luminaria</button>"
            //             "</form>"
            //             "<form action=\"./buzzer\" method=\"GET\">"
            //                 "<button type=\"submit\">🔔 Campainha</button>"
            //             "</form>"
            //             "<form action=\"./water\" method=\"GET\">"
            //                 "<button type=\"submit\">🚿 Mangueira</button>"
            //             "</form>"
            //         "</div>"
            //         "<p class=\"temperature\">🌡️ Temperatura Interna: %.2f &deg;C</p>"
            //     "</body>"
            // "</html>"
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n"
            "\r\n"
            "<!DOCTYPE html>"
            "<html>"
                "<head>"
                    "<title>🏠 Controle</title>"
                    "<meta charset=\"UTF-8\">"
                    "<style>"
                        "body{background:#f8f9fa;font-family:Arial;margin:0;min-height:100vh;display:flex;flex-direction:column;}"
                        ".container{max-width:800px;margin:0 auto;padding:20px;}"
                        ".card{background:#fff;border-radius:10px;box-shadow:0 4px 6px rgba(0,0,0,0.1);padding:20px;margin-bottom:20px;}"
                        ".btn{display:inline-flex;align-items:center;justify-content:center;"
                        "background:#6c757d;color:white;border:none;border-radius:5px;"
                        "padding:12px 24px;font-size:18px;margin:8px;cursor:pointer;transition:all 0.3s;}"
                        ".btn:hover{opacity:0.8;transform:translateY(-2px);}"
                        ".btn-primary{background:#0d6efd;}.btn-danger{background:#dc3545;}"
                        ".btn-success{background:#198754;}.btn-warning{background:#ffc107;color:#000;}"
                        ".form-group{margin-bottom:1rem;display:flex;align-items:center;}"
                        "select{padding:8px;border-radius:4px;border:1px solid #ced4da;margin-left:10px;}"
                        "h1{color:#212529;margin-bottom:1.5rem;}"
                        ".temp-display{font-size:1.5rem;color:#495057;margin-top:1rem;}"
                    "</style>"
                    "<script>"
                        "function handleSubmit(e,form){"
                            "e.preventDefault();"
                            "fetch(form.action+'?'+new URLSearchParams(new FormData(form)),{method:'GET'})"
                            ".then(()=>window.location.reload());"
                        "}"
                    "</script>"
                "</head>"
                "<body>"
                    "<div class=\"container\">"
                        "<h1>🏠 Controle Residencial</h1>"
                    "<div class=\"card\">"
                        "<form action=\"./lamp\" method=\"GET\" class=\"form-group\">"
                            "<select name=\"level\" required>"
                                "<option value=\"high\" %s>Alto</option>"
                                "<option value=\"so-so\" %s>Medio</option>"
                                "<option value=\"low\" %s>Baixo</option>"
                                "<option value=\"none\" %s>Desligar</option>"
                            "</select>"
                            "<button type=\"submit\" class=\"btn btn-primary\">💡 Luminaria</button>"
                        "</form>"
                        "<form action=\"./buzzer\" method=\"GET\">"
                            "<button type=\"submit\" class=\"btn btn-warning\">🔔 Campainha</button>"
                        "</form>"
                        "<form action=\"./water\" method=\"GET\">"
                            "<button type=\"submit\" class=\"btn btn-success\">🚿 Mangueira</button>"
                        "</form>"
                    "</div>"
                        "<p class=\"temp-display\">🌡️ Temperatura: %.2f°C</p>"
                    "</div>"
                "</body>"
            "</html>", led_high, led_soso, led_low, led_none, led_high, led_soso, led_low, led_none, led_high, led_soso, led_low, led_none, temperature);

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

