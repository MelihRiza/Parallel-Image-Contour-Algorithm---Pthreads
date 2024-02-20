
======================================================TEMA1-APD====================================================

student: Riza Melih
grupa: 334 CD


Main: 

	Initializez 'countour_map' si 'scaled_image' care vor face parte din structura Parameters si vor trebui
share-uite intre threaduri.
	
	Structura Parameters: contine P - nr de thread-uri; id - id ul thread-ului; argv2 - argv[2] numele fisierului
de output; step_x; step_y; barrier - bariera folosita pentru sincronizare in functia paralela; image - imaginea 
initiala; countour_map; scaled_image.
	
	Structura Thread: contine un camp Parameters **parameters care va stoca array-ul de parametri si id, id-ul
thread-ului curent.
	
	Ideea din spatele 'Thread' este bazata pe share-uirea de date intre thread-uri. Fiecare thread va avea acces 
la acelasi array de parameters. In array-ul parameters, parameters[0] este responsabil pentru thread-ul 0, 
parameters[1] pentru thread-ul 1 si tot asa. Astfel fiecare thread avand acces la intregul array, are acces la 
parametrii tuturor celorlalte thread-uri.

	Se pornesc thread-urile:
	
paralelize_algorithm:

	Sincronizare intre pasii descrisi mai jos am realizat-o cu o bariera. Intre ficare dintre actiuni, se va afla
o bariera pentru a impiedica lucrul unor thread-uri cu memoria in care s-ar putea sa scrie alte thread-uri.

	-> Paralelizez construirea 'contour_map' fiecare thread luand un chunck de dimensiune CONTOUR_CONFIG_COUNT / P.
	-> Sincronizez thread-urile cu bariera specificata anterior, pentru a nu lucra cu o zona de memorie la care
un alt thread ar putea inca scrie.
	-> Verific daca imaginea initiala se incadreaza in limitele dimensiunilor, fac acest lucru doar intr-un
thread, thread-ul 0. (nu are rost sa fac verificarea in toate thread-urile). Daca imaginea se incadreaza in limite,
atribui ficarui scaled_image (din fiecare thread), valoarea imaginii corespunzatoare thread-ului (cu alte cuvinte,
scaled_image e imaginea initiala). Altfel, modific parametrii x si y si aloc memorie pentru imaginea rescalata.
	-> In cazul in care imaginea trebuie rescalata, realizez acest lucru paralelizat, la fel fiecarui thread
corespunzandu-i cate un chunck din memorie de dimensiune egala cu celelalte. Urmeaza o sincronizare a thread-urilor.
	-> Aloc memorie pentru grid, fac acest lucru o singura data (in thread-ul 0), apoi in restul intrarilor
din array-ul parameters propag adresa unde s-a facut malloc-ul pentru a oferi tutror thread-urilor aceasta adresa.
	-> Apoi aloc memorie pentru fiecare linie in grid, acest lucru se poate face in paralel, avand fiecare
thread acces la adresa grid-ului dat anterior.
	-> Urmeaza transformarea grid-ului in valori binare, lucru efectuat in paralel.
	-> Calcularea valorilor din fiecare "patratel" si atribuirea contururilor.
	-> Scriea rezultatului intr-un fisier de output il realizez in thread-ul 0, nu are rost paralelizarea.
	-> La fel si dezalocarea memoriei o realizez prin thread-ul 0, fiecare camp din structura Parameters contine
aceleasi adrese pentru ficare thread, astfel este de ajuns sa dezaloc memoria dintr-un singur thread.

	Se termina executia in paralel a thread-urilor si acestea sunt 'asteptate' si terminate cu pthread_join in 
main. Apoi dezaloc memoria ocupata de structurile alocate anterior in main si 'scaled_image'. Se incheie programul.
	 
