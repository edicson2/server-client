/* This `define` tells unistd to define usleep and random.  */
#define _XOPEN_SOURCE 500
#include <netinet/in.h>
#include "client_thread.h"
#include <string.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/socket.h>
//#include <time.h>
//#include <stdlib.h>

int port_number = -1;
int num_request_per_client = -1;
int num_resources = -1;
int *provisioned_resources = NULL;

//Variable d'initialisation des threads clients.
unsigned int count = 0;


// Variable du journal.
// Nombre de requête acceptée (ACK reçus en réponse à REQ)
unsigned int count_accepted = 0;

// Nombre de requête en attente (WAIT reçus en réponse à REQ)
unsigned int count_on_wait = 0;

// Nombre de requête refusée (REFUSE reçus en réponse à REQ)
unsigned int count_invalid = 0;

// Nombre de client qui se sont terminés correctement (ACC reçu en réponse à END)
unsigned int count_dispatched = 0;

// Nombre total de requêtes envoyées.
unsigned int request_sent = 0;
int etat=0;

/*********************************************************************************************************/
int connect_ct()
{
  //struct sockaddr_in address;
  int sock = 0;
  struct sockaddr_in serv_addr;

  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    printf("\n Socket creation error \n");
    return -1;
  }

  memset(&serv_addr, '0', sizeof(serv_addr));

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port_number);
  serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
  {
    printf("\nConnection echouée \n");
    exit(0);
    //return -1;
  }

  //send(sock , hello , strlen(hello) , 0 );

  return sock;
}



void envoyer_ressource(int socket){
//on va construire un tableau pour envoyer.
  //int buffe[5]={11,3,33,45,55};
  int provision_res[num_resources+2];

  for(int i=0; i<num_resources+2; i++){
    if(i==0)
      provision_res[i]=CONF;
    else if(i==1)
      provision_res[i]=num_resources;
    else
      provision_res[i]=provisioned_resources[i-2];
  }
  send(socket, provision_res, sizeof(provision_res) ,0);


  struct cmd_header_t header = { .nb_args = 0};

  int len=read_socket(socket, &header, sizeof(header),8);
  printf("len %d\n", len);
  if(len>0){
    if(header.cmd==ACK){
      printf("configuration du serveur reussie\n\n");
    } else{
      printf("La réponse du serveur est différente a l'ésperé\n\n");
      //exit(1);
    }
  } else{
    printf("Échec dans la configuration du serveur... \n\n");
    //exit(1);
  }

  close(socket);

}

void envoyer_begin(int socket,int rng){
  int id_ct[3];//{0,1,6};

  id_ct[0]=BEGIN;
  id_ct[1]=1;
  id_ct[2]=rng;

  send(socket ,id_ct, sizeof(id_ct) ,0);

  struct cmd_header_t header = { .nb_args = 0};

  int len=read_socket(socket, &header, sizeof(header),8);

  if(len>0){
    if(header.cmd==ACK){
      printf("*******************Accuse de Reception**************************\n");
      printf("ACK\n\n");
      etat=1;
    }
    else{}
  }
  else{}
  close(socket);

}

// pour faire la configuration du serveur
void envoie_config(int nb_cl){
  int socket_fd=connect_ct();
  envoyer_begin(socket_fd,nb_cl);

  int socket_fd2 = connect_ct();

  envoyer_ressource(socket_fd2);

}

void client_peut_connecter(int socket,int id){
  printf("Client %d veut connecter\n",id);
  if(socket<0){
    printf("connection refusé client %d\n",id);
    pthread_exit (NULL);
  }
  else{
    printf("connection reussi client %d.\n",id);
  }
}

void envoyer_INI(int socket,int id, int cmd){
  //Initialisation du client

  int max[num_resources];
  int ini_res[num_resources+3];
  for(int i = 0; i <num_resources; i++)
  {
    //allocations[i] = 0;
    max[i] = rand() % (provisioned_resources[i]+1);
  }
  for(int i=0; i<num_resources+3; i++){
    if(i==0)
      ini_res[i]=cmd;
    else if(i==1)
      ini_res[i]=num_resources;
    else if(i==2)
      ini_res[i]=id;  //330+id;
    else
      ini_res[i]=max[i-3];
  }
  send(socket,ini_res,sizeof(ini_res),0);
  struct cmd_header_t header = { .nb_args = 0};
  int len=read_socket(socket, &header, sizeof(header),8);

  if(len>0) {
    if (header.cmd == ACK) {
      //printf("ACK \n");
    }
  } else{
    printf("Pas de reponse!\n");
  }
  //close(socket);

}

/*********************************************************************************************************/
// Vous devez modifier cette fonction pour faire l'envoie des requêtes
// Les ressources demandées par la requête doivent être choisies aléatoirement
// (sans dépasser le maximum pour le client). Elles peuvent être positives
// ou négatives.
// Assurez-vous que la dernière requête d'un client libère toute les ressources
// qu'il a jusqu'alors accumulées.
void
send_request (int client_id, int request_id, int socket_fd) {

  // TP2 TODO

  //fprintf (stdout, "Client %d is sending its %d request\n", client_id,
  // request_id);
  envoyer_INI(socket_fd, client_id, REQ);

  // TP2 TODO:END

}

void *
ct_code (void *param)
{
  int socket_fd = -1;
  client_thread *ct = (client_thread *) param;

  socket_fd=connect_ct();
  if(etat==0){
    exit(0);}
  client_peut_connecter(socket_fd,ct->id);
  envoyer_INI(socket_fd,ct->id, INIT);

  // TP2 TODO
  // Vous devez ici faire l'initialisation des petits clients (`INI`).
  // TP2 TODO:END
  // printf("***********************Usage maximum**********************\n");


  //int tab[3] = {REQ, 1, rand()};

  for (unsigned int request_id = 0; request_id < num_request_per_client;
       request_id++) {
    socket_fd=connect_ct();
    // TP2 TODO
    // Vous devez ici coder, conjointement avec le corps de send request,
    // le protocole d'envoi de requête.
    //printf("Sending REQ\n");
    //envoyer_INI(socket_fd, ct->id, REQ);

    //send(socket_fd, tab, sizeof(tab), 0);
    //envoyer_INI(socket_fd,ct->id,  REQ);
    send_request (ct->id, request_id, socket_fd);

    // TODO Recevoir et gerer reponses (Wait, Close, etc...)

// TP2 TODO:END

    /* Attendre un petit peu (0s-0.1s) pour simuler le calcul.  */
    usleep (random () % (100 * 1000));

    /* struct timespec delay;
     * delay.tv_nsec = random () % (100 * 1000000);
     * delay.tv_sec = 0;
     * nanosleep (&delay, NULL); */

  }
  //free(tab);
  close(socket_fd);
  pthread_exit (NULL);
}


//
// Vous devez changer le contenu de cette fonction afin de régler le
// problème de synchronisation de la terminaison.
// Le client doit attendre que le serveur termine le traitement de chacune
// de ses requêtes avant de terminer l'exécution.
//
void
ct_wait_server ()
{

  // TP2 TODO

  sleep (4);

  // TP2 TODO:END

}


void
ct_init (client_thread * ct)
{
  ct->id = count++;
}

void
ct_create_and_start (client_thread * ct)
{
  pthread_attr_init (&(ct->pt_attr));
  pthread_attr_setdetachstate(&(ct->pt_attr), PTHREAD_CREATE_DETACHED);
  pthread_create (&(ct->pt_tid), &(ct->pt_attr), &ct_code, ct);
}

//
// Affiche les données recueillies lors de l'exécution du
// serveur.
// La branche else ne doit PAS être modifiée.
//
void
st_print_results (FILE * fd, bool verbose)
{
  if (fd == NULL)
    fd = stdout;
  if (verbose)
  {
    fprintf (fd, "\n---- Résultat du client ----\n");
    fprintf (fd, "Requêtes acceptées: %d\n", count_accepted);
    fprintf (fd, "Requêtes : %d\n", count_on_wait);
    fprintf (fd, "Requêtes invalides: %d\n", count_invalid);
    fprintf (fd, "Clients : %d\n", count_dispatched);
    fprintf (fd, "Requêtes envoyées: %d\n", request_sent);
  }
  else
  {
    fprintf (fd, "%d %d %d %d %d\n", count_accepted, count_on_wait,
             count_invalid, count_dispatched, request_sent);
  }
}