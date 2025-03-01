#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>
#include <map>
#include <vector>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

using namespace std;

char vec_hash[MAX_CHUNKS][HASH_SIZE + 1]; // vector de hashuri
pthread_mutex_t mutex; // mutex pentru a proteja accesul la mapul clientului

// map pentru fiecare client, un map dintre nume fisier si un vector de hashuri
map<string, vector<string>> lista_client;

// numarul de fisiere dorite
int nr_fisiere_dorite;

// vectorul cu numele fisierelor dorite
vector<string> fisiere_dorite;

// map pe care il retine trackerul, un map dintre nume fisier si o pereche de vectori, unul de peers si unul de hashuri
map<string, pair<vector<int>, vector<string>>> lista_tracker;

// functie pentru a citi datele din fisierul de intrare
void citire_fisier(int numtasks, int rank) {
    // numele fisierului de intrare
    char nume_fisier[MAX_FILENAME] = "in1.txt\0";
    nume_fisier[2] = rank + '0';

    FILE *f;
    f = fopen(nume_fisier, "r");
    if (f == NULL) {
        printf("Nu am putut deschide fisierul %s\n", nume_fisier);
        exit(-1);
    }

    // citesc numarul de fisiere dorite
    int nr_fisiere;
    fscanf(f, "%d", &nr_fisiere);

    char nume_fisier2[MAX_FILENAME];
    int nr_hashuri;
    char hash[HASH_SIZE + 1];

    // citesc continutul fisierului
    for (int i = 0; i < nr_fisiere; i++) {
        fscanf(f, "%s", nume_fisier2);
        fscanf(f, "%d", &nr_hashuri);

        // citesc hashurile
        for (int j = 0; j < nr_hashuri; j++) {
            fscanf(f, "%s", hash);
            lista_client[nume_fisier2].push_back(hash);
        }
    }

    // citesc numarul de fisiere dorite
    fscanf(f, "%d", &nr_fisiere_dorite);

    // citesc numele fisierelor dorite
    for (int i = 0; i < nr_fisiere_dorite; i++) {
        fscanf(f, "%s", nume_fisier2);
        fisiere_dorite.push_back(nume_fisier2);
    }

    fclose(f);
}

// functie pentru a transmite datele catre tracker
void transmitere_catre_tracker(int numtasks, int rank) {
    int nr_hashuri;

    // trimit numarul de fisiere
    int nr_fisiere = lista_client.size();
    MPI_Send(&nr_fisiere, 1, MPI_INT, TRACKER_RANK, rank, MPI_COMM_WORLD);

    // iterator prin mapul ce contine numele fisierelor si hashurile
    for (auto it = lista_client.begin(); it != lista_client.end(); it++) {
        // trimit numele fisierului
        MPI_Send(it->first.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, rank + 10, MPI_COMM_WORLD);

        // trimit numarul de hashuri
        nr_hashuri = it->second.size();
        MPI_Send(&nr_hashuri, 1, MPI_INT, TRACKER_RANK, rank + 20, MPI_COMM_WORLD);

        for (int i = 0; i < nr_hashuri; i++) {
            strcpy(vec_hash[i], it->second[i].c_str());
        }

        // trimit hashurile
        MPI_Send(vec_hash, nr_hashuri * (HASH_SIZE + 1), MPI_CHAR, TRACKER_RANK, rank + 30, MPI_COMM_WORLD);
    }
}

void *download_thread_func(void *arg) {
    int rank = *(int *)arg;

    char nume_fisier[MAX_FILENAME];
    int nr_hashuri;
    int nr_peers;
    int contor = 0;  // pentru a numara cate cereri s-au facut

    int hashuri_primite = 0;  // numarul de hashuri primite
    int index_cui_cere = 0;  // indexul cui ii cere hashul pe care vrea sa il descarce
    int raspuns = 0;  // raspunsul primit de la peer , 1 daca a primit hashul, 0 altfel

    vector<int> vec_peers; // vectorul de peers (contine seed si peer)
    int peer;

    for (int i = 0; i < nr_fisiere_dorite; i++) {
        strcpy(nume_fisier, fisiere_dorite[i].c_str());

        // trimit numele fisierului
        MPI_Send(nume_fisier, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 2000, MPI_COMM_WORLD);

        // primesc numarul de peers
        MPI_Recv(&nr_peers, 1, MPI_INT, TRACKER_RANK, rank + 50, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // primesc vectorul de peers
        vec_peers.clear();
        for(int j = 0; j < nr_peers; j++){
            MPI_Recv(&peer, 1, MPI_INT, TRACKER_RANK, rank + 60, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            vec_peers.push_back(peer);
        }

        // primesc numarul de hashuri
        MPI_Recv(&nr_hashuri, 1, MPI_INT, TRACKER_RANK, rank + 70, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // primesc hashurile
        MPI_Recv(vec_hash, nr_hashuri * (HASH_SIZE + 1), MPI_CHAR, TRACKER_RANK, rank + 80, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        hashuri_primite = 0;
        index_cui_cere = nr_peers - 1;
        contor = 0;

        while (hashuri_primite < nr_hashuri) {
            if (contor != 0 && contor % 10 == 0) {
                // trimit numele fisierului
                MPI_Send(nume_fisier, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 2000, MPI_COMM_WORLD);

                // primesc numarul de peers
                MPI_Recv(&nr_peers, 1, MPI_INT, TRACKER_RANK, rank + 100, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                // primesc vectorul de peers
                vec_peers.clear();
                for(int j = 0; j < nr_peers; j++){
                    MPI_Recv(&peer, 1, MPI_INT, TRACKER_RANK, rank+110, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    vec_peers.push_back(peer);
                }

                contor = 0;
            }

            // trimit numele fisierului
            MPI_Send(nume_fisier, MAX_FILENAME, MPI_CHAR, vec_peers[index_cui_cere], 1000, MPI_COMM_WORLD);
            // trimit hashul pe care il vreau
            MPI_Send(vec_hash[hashuri_primite], HASH_SIZE + 1, MPI_CHAR, vec_peers[index_cui_cere], 1001, MPI_COMM_WORLD);

            // primesc raspunsul
            MPI_Recv(&raspuns, 1, MPI_INT, vec_peers[index_cui_cere], rank + 90, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // daca raspunsul este 1 inseamna ca am primit hashul
            if (raspuns == 1) {
                pthread_mutex_lock(&mutex);
                lista_client[nume_fisier].push_back(vec_hash[hashuri_primite]);
                pthread_mutex_unlock(&mutex);
                hashuri_primite++;
            }

            contor++;

            index_cui_cere -= 1;
            if (index_cui_cere == -1) {
                index_cui_cere = nr_peers - 1;
            }
        }

        // scriu in fisier hashurile descarcate
        FILE *f;
        char nume_fisier2[MAX_FILENAME] = "client1_";
        nume_fisier2[6] = rank + '0';
        for (long unsigned int i = 0; i < strlen(nume_fisier); i++) {
            nume_fisier2[8 + i] = nume_fisier[i];
        }

        f = fopen(nume_fisier2, "w");

        // scriu hashurile in fisier
        for (long unsigned int i = 0; i < lista_client[nume_fisier].size() - 1; i++) {
            fprintf(f, "%s\n", lista_client[nume_fisier][i].c_str());
        }
        int aux = lista_client[nume_fisier].size() - 1;
        fprintf(f, "%s", lista_client[nume_fisier][aux].c_str());

        fclose(f);
    }

    // trimit la tracker ca am terminat de descarcat toate fisierele
    char mesaj[MAX_FILENAME] = "terminat\0";
    MPI_Send(mesaj, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 2000, MPI_COMM_WORLD);

    return NULL;
}

void *upload_thread_func(void *arg) {
    MPI_Status status;
    char hash[HASH_SIZE + 1];
    int raspuns = 0;
    char nume_fisier[MAX_FILENAME];

    while (1) {
        // primesc numele fisierului sau mesaj de la tracker
        MPI_Recv(nume_fisier, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, 1000, MPI_COMM_WORLD, &status);

        // inseamna ca am primit mesaj de la tracker ca au terminat toti clientii si ma opresc
        if (status.MPI_SOURCE == TRACKER_RANK) {
            break;
        }

        // daca nu am primit mesaj de la tracker, inseamna ca am primit cerere de la un client si primesc hashul
        MPI_Recv(hash, HASH_SIZE + 1, MPI_CHAR, MPI_ANY_SOURCE, 1001, MPI_COMM_WORLD, &status);
        raspuns = 0;

        // parcurg map-ul lista_client si verific daca hashul este in map
        pthread_mutex_lock(&mutex);
        for(long unsigned int i=0;i<lista_client[nume_fisier].size();i++){
            if(strcmp(lista_client[nume_fisier][i].c_str(), hash) == 0){
                raspuns = 1;
                break;
            }
        }
        pthread_mutex_unlock(&mutex);

        // trimit raspunsul, 1 daca am gasit hashul, 0 altfel
        MPI_Send(&raspuns, 1, MPI_INT, status.MPI_SOURCE, status.MPI_SOURCE + 90, MPI_COMM_WORLD);
    }

    return NULL;
}

void tracker_func(int numtasks, int rank) {
    char nume_fisier[MAX_FILENAME];
    int nr_fisiere;
    int nr_hashuri;
    vector<string> hashuri;

    for (int i = 1; i < numtasks; i++) {
        // primesc numarul de fisiere
        MPI_Recv(&nr_fisiere, 1, MPI_INT, i, i, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        for (int j = 0; j < nr_fisiere; j++) {
            // primesc numele fisierului
            MPI_Recv(nume_fisier, MAX_FILENAME, MPI_CHAR, i, i + 10, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // primesc numarul de hashuri
            MPI_Recv(&nr_hashuri, 1, MPI_INT, i, i + 20, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // primesc hashurile
            MPI_Recv(vec_hash, nr_hashuri * (HASH_SIZE + 1), MPI_CHAR, i, i + 30, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            hashuri.clear();
            for (int j = 0; j < nr_hashuri; j++) {
                hashuri.push_back(vec_hash[j]);
            }

            // daca fisierul nu exista in lista_tracker, adaug hashurile si peer-ul
            // daca exista, adaug doar peer-ul
            if (lista_tracker.find(nume_fisier) == lista_tracker.end()) {
                vector<int> peers = {i};
                pair<vector<int>, vector<string>> p = make_pair(peers, hashuri);
                lista_tracker[nume_fisier] = p;

            } else {
                lista_tracker[nume_fisier].first.push_back(i);
            }
        }
    }

    // trimit la toti clientii ca pot incepe descarcarea
    int ok_incepe = 1;
    for (int i = 1; i < numtasks; i++) {
        MPI_Send(&ok_incepe, 1, MPI_INT, i, i + 40, MPI_COMM_WORLD);
    }

    int nr = 0;
    MPI_Status status;

    int index = 0;
    int tag = 0;
    int ok = 1;
    int size = 0;

    map<string, vector<int>> lista_cerere; // contine pentru fiecare fisier lista cu clientii care au cerut fisierul
    char mesaj[MAX_FILENAME];

    while (1) {
        // primesc mesaj de la client
        MPI_Recv(mesaj, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, 2000, MPI_COMM_WORLD, &status);

        if (strcmp(mesaj, "terminat\0") == 0) {
            nr++;

            // daca am primit de la toti clientii ca au terminat de descarcat se opreste
            if (nr == numtasks - 1) {
                break;
            }

        } else {
            index = status.MPI_SOURCE;
            tag = status.MPI_SOURCE;
            ok = 1;

            // verific daca clientul a mai cerut fisierul, daca nu ii trimit si hashurile pt a sti ce are de descarcat
            for (long unsigned int i = 0; i < lista_cerere[mesaj].size(); i++) {
                if (index == lista_cerere[mesaj][i]) {
                    ok = 0;
                    break;
                }
            }

            if (ok == 0) {
                // trimit numarul de peers
                size = lista_tracker[mesaj].first.size();
                MPI_Send(&size, 1, MPI_INT, status.MPI_SOURCE, tag + 100, MPI_COMM_WORLD);

                // trimti vectorul de peers
                for(int i=0; i<size; i++){
                    MPI_Send(&lista_tracker[mesaj].first[i], 1, MPI_INT, status.MPI_SOURCE, tag+110, MPI_COMM_WORLD);
                }


            } else if (ok == 1) {
                // trimit numarul de peers
                size = lista_tracker[mesaj].first.size();
                MPI_Send(&size, 1, MPI_INT, status.MPI_SOURCE, tag + 50, MPI_COMM_WORLD);
                
                // trimit vectorul de peers
                for(int i = 0; i < size; i++){
                    MPI_Send(&lista_tracker[mesaj].first[i], 1, MPI_INT, status.MPI_SOURCE, tag + 60, MPI_COMM_WORLD);
                }

                // il adaug in lista de cerere
                lista_cerere[mesaj].push_back(index);
                size = lista_tracker[mesaj].second.size();

                // trimit numarul de hashuri
                MPI_Send(&size, 1, MPI_INT, status.MPI_SOURCE, tag + 70, MPI_COMM_WORLD);

                for (int j = 0; j < size; j++) {
                    strcpy(vec_hash[j], lista_tracker[mesaj].second[j].c_str());
                }

                // trimit hashurile
                MPI_Send(vec_hash, size * (HASH_SIZE + 1), MPI_CHAR, status.MPI_SOURCE, tag + 80, MPI_COMM_WORLD);

                // adaug la vectorul de peers si clientul curent
                lista_tracker[mesaj].first.push_back(index);
            }
        }
    }

    // trimit la toti clientii ca au terminat toti de descarcat
    char mesaj2[MAX_FILENAME] = "terminat\0";
    for (int i = 1; i < numtasks; i++) {
        MPI_Send(mesaj2, MAX_FILENAME, MPI_CHAR, i, 1000, MPI_COMM_WORLD);
    }
}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *)&rank);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *)&rank);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }
}

int main(int argc, char *argv[]) {
    int numtasks, rank;

    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker_func(numtasks, rank);
    } else {
        pthread_mutex_init(&mutex, NULL);
        citire_fisier(numtasks, rank);
        transmitere_catre_tracker(numtasks, rank);

        int ok_incepe;
        // astept sa primesc de la tracker ca pot incepe descarcarea
        MPI_Recv(&ok_incepe, 1, MPI_INT, TRACKER_RANK, rank + 40, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        peer(numtasks, rank);
        pthread_mutex_destroy(&mutex);
    }

    MPI_Finalize();
}
