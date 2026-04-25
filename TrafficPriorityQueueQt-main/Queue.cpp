#include <iostream>
using namespace std;

class vehicle{
public:
    int id;
    bool isEmergency;
    
    vehicle(int i=0, bool e=false){
        id=i;
        isEmergency=e;
    }
};


class Node{
public:
    vehicle data;
    Node *next;
    
    Node(vehicle v){
        data =v;
        next = NULL;
    }
};

class Queue{
private:
    Node *front;
    Node *rear;
    
public:
    Queue(){
        front = NULL;
        rear= NULL;
    }
    
    bool isEmpty(){//checks if lane is empty
        return front ==NULL;
    }
    
    void enqueue(vehicle v){//Adds a vehicle to the end of the queue
        Node * temp= new Node(v);
        if (rear==NULL){
            rear=front=temp;
        }
        else{
            rear->next= temp;
            rear=temp;
        }
    }
    
    vehicle dequeue(){//Removes a vehicle from the beginning of the queue
        if (isEmpty()){
            cout<<"Queue is empty.";
            return vehicle (-1, false);
        }
        
        else{
            Node *temp= front;
            front= front ->next;
            vehicle v= temp->data;
            
            delete temp;
            return v;
        }
    }
    
    vehicle top(){//Returns the data of the first vehicle in the queue
        if (!isEmpty()){
            return front->data;
        }
        return vehicle(-1, false);
    }
    
    int size(){//Returns the size of the queue, lane.
        Node * temp= front;
        int count=0;
        while (temp!=NULL){
            count++;
            temp=temp->next;
        }
        return count;
    }
    
};


int main (){
    Queue q;
    
    q.enqueue(vehicle(1234, false));
    q.enqueue(vehicle(4321, true));
    
    cout<<"Size of lane: "<<q.size()<<endl;
    cout<<"Front:"<<q.top().id<<", "<<q.top().isEmergency<<endl;
    q.dequeue();
    
    cout<<"Front: "<<q.top().id<<", "<<q.top().isEmergency<<endl;
    
    
    return 0;
}

