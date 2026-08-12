#include "game.hpp"

void Game::copy_ascii(const char* source,char* target,uint8_t capacity){
    if(source==NULL||target==NULL||capacity==0U){return;}
    uint8_t index=0U;
    while(source[index]!='\0'&&(uint8_t)(index+1U)<capacity){target[index]=source[index];index++;}
    target[index]='\0';
}

void Game::format_u32(uint32_t value,char* text){
    char reverse[10];uint8_t count=0U;
    if(text==NULL){return;}
    if(value==0U){text[0]='0';text[1]='\0';return;}
    while(value!=0U&&count<10U){reverse[count++]=(char)('0'+value%10U);value/=10U;}
    for(uint8_t i=0U;i<count;i++){text[i]=reverse[count-1U-i];}
    text[count]='\0';
}

void Game::format_activity(const GameActivity& activity,char* text){
    if(text==NULL){return;}
    if(activity.valid==0U||activity.year==0U){text[0]='-';text[1]='-';text[2]='\0';return;}
    uint8_t index=0U;
    const uint8_t values[5]={activity.month,activity.day,activity.hour,activity.minute,activity.second};
    for(uint8_t i=0U;i<5U;i++){
        text[index++]=(char)('0'+values[i]/10U);text[index++]=(char)('0'+values[i]%10U);
        if(i==0U){text[index++]='-';}else if(i==1U){text[index++]=' ';}else if(i==2U||i==3U){text[index++]=':';}
    }
    text[index]='\0';
}
