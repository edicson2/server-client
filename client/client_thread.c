/* This `define` tells unistd to define usleep and random.  */
#define _XOPEN_SOURCE 500
#include <netinet/in.h>
#include "client_thread.h"

#include <sys/types.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <memory.h>
#include <arpa/inet.h>
#include <time.h>

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



int send_ct (int socket, void *tab) {

  send(socket, &tab, sizeof(tab), 0);

  close(socket);
  return 0;
}


// Vous devez modifier cette fonction pour faire l'envoie des requêtes
// Les ressources demandées par la requête doivent être choisies aléatoirement
// (sans dépasser le maximum pour le client). Elles peuvent être positives
// ou négatives.
// Assurez-vous que la dernière requête d'un client libère toute les ressources
// qu'il a jusqu'alors accumulées.
void
send_request (int client_id, int request_id, int socket_fd)
{

  // TP2 TODO

  fprintf (stdout, "Client %d is sending its %d request\n", client_id,
           request_id);
  //send_ct(socket_fd);

  // TP2 TODO:END

}

int creer_socket() {
  int socket_ct;
  if ((socket_ct = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    printf("\n Socket creation error \n");
    return -1;
  }
  return socket_ct;
}


void connect_ct(int socket_ct)
{
  struct sockaddr_in serv_addr;

  memset(&serv_addr, '0', sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port_number);

  // Convert IPv4 and IPv6 addresses from text to binary form
  if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr)<=0)
  {
    printf("\nInvalid address/ Address not supported \n");
    exit(1);
  }
  // Attendre jusqu'á obtenir une conection
  while (connect(socket_ct, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0);
}


void recevoir_reponse(int socket_ct) {

  struct cmd_header_t header = { .nb_args = 0 };

  int len = read_socket(socket_ct, &header, sizeof(header), 30 * 1000);

  if (len > 0) {
    if (len != sizeof(header.cmd) && len != sizeof(header)) {
      printf ("Thread %d received invalid command size=%d!\n", len, len);
    } else {
      printf("Thread %d received command=%d, nb_args=%d\n", len, header.cmd, header.nb_args);
      switch (header.cmd) {
        case 4:
          if(header.nb_args == 0) {
            printf("requete de ressources exécuté!\n");
          } else {
            printf("ACK de commencement!\n");
            printf("Value received is %d\n", header.cmd);
          }
          break;
        case 5: printf("WAIT!\n"); break;
        case 8: printf("ERR!\n"); break;
        default: printf("Erreur!!\n"); break;
      }
      // dispatch of cmd void thunk(int sockfd, struct cmd_header* header);
    }
  } else {
    if (len == 0) {
      fprintf(stderr, "Thread %d, connection timeout\n", 0);
    }
  }
}

void envoyer_configuration_initial(int socket_fd) {

  // Envoyer la requete pour commencer le serveur
  int rng = rand(); // random number to send to the server
  int begin[3] = {0, 1, rng};
  send(socket_fd, begin, sizeof(begin), 0);

  // TODO mutex pour l'acces
  request_sent++;
  // TODO fintex fin

  recevoir_reponse(socket_fd);

  // TODO Verifier que le nombre retourné rng est le meme


  printf("");
//

  int *allocations = (int*)malloc(num_resources * sizeof(int));
  int *max = (int*)malloc(num_resources * sizeof(int));
  for(int i = 0; i < num_resources; i++) {
    allocations[i] = 0;
    max[i] = rand() % (provisioned_resources[i]+1);
  }

  char ini[200];
  sprintf(ini, "1 %d", max[0]);
  for(int j = 1; j < num_resources; j++)
  {
    sprintf(ini, "%s %d", ini, max[j]);
  }
  sprintf(ini, "%s\n", ini);
  int r[3] = {1, 2, 4, 4};
  send(socket_fd, r, sizeof(r), 0);
  recevoir_reponse(socket_fd);

}



void *
ct_code (void *param)
{
  int socket_fd = -1;
  client_thread *ct = (client_thread *) param;

  // TP2 TODO
  // Vous devez ici faire l'initialisation des petits clients (`INI`).

  // Creer le socket et faire la connection
  socket_fd = creer_socket();
  connect_ct(socket_fd);

  envoyer_configuration_initial(socket_fd);




  // TP2 TODO:END

  for (unsigned int request_id = 0; request_id < num_request_per_client;
       request_id++)
  {

    // TP2 TODO
    // Vous devez ici coder, conjointement avec le corps de send request,
    // le protocole d'envoi de requête.

    //send_request (ct->id, request_id, socket_fd);

    // TP2 TODO:END

    /* Attendre un petit peu (0s-0.1s) pour simuler le calcul.  */
    usleep (random () % (100 * 1000));
    /* struct timespec delay;
     * delay.tv_nsec = random () % (100 * 1000000);
     * delay.tv_sec = 0;
     * nanosleep (&delay, NULL); */
  }
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












//**************************************//
