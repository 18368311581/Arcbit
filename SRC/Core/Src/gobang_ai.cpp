#include "gobang_ai.hpp"
#include "rng.h"

namespace {

static constexpr uint8_t BoardSize=15U;

struct Score96 {
    uint32_t low;
    uint32_t middle;
    uint32_t high;
};

struct DirectionScore {
    uint32_t ai;
    uint32_t player;
    bool ai_multiply;
    bool player_multiply;
};

static const int8_t directions[4][2]={{-1,0},{-1,-1},{0,-1},{1,-1}};
static const uint8_t board_score[BoardSize*BoardSize]={
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,
    0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,
    0,0,1,1,2,2,2,2,2,2,2,1,1,0,0,
    0,0,1,1,2,2,2,2,2,2,2,1,1,0,0,
    0,0,1,1,2,2,3,3,3,2,2,1,1,0,0,
    0,0,1,1,2,2,3,3,3,2,2,1,1,0,0,
    0,0,1,1,2,2,3,3,3,2,2,1,1,0,0,
    0,0,1,1,2,2,2,2,2,2,2,1,1,0,0,
    0,0,1,1,2,2,2,2,2,2,2,1,1,0,0,
    0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,
    0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static Score96 score_from_u32(uint32_t value){return {value,0U,0U};}

static void score_add(Score96* target,const Score96& value){
    uint64_t sum=(uint64_t)target->low+value.low;
    target->low=(uint32_t)sum;
    sum=(uint64_t)target->middle+value.middle+(sum>>32U);
    target->middle=(uint32_t)sum;
    target->high=(uint32_t)((uint64_t)target->high+value.high+(sum>>32U));
}

static void score_add_u32(Score96* target,uint32_t value){score_add(target,score_from_u32(value));}

static void score_multiply(Score96* target,uint32_t factor){
    uint64_t product=(uint64_t)target->low*factor;
    target->low=(uint32_t)product;
    product=(uint64_t)target->middle*factor+(product>>32U);
    target->middle=(uint32_t)product;
    product=(uint64_t)target->high*factor+(product>>32U);
    target->high=(uint32_t)product;
}

static int8_t score_compare(const Score96& left,const Score96& right){
    if(left.high!=right.high){return left.high>right.high?1:-1;}
    if(left.middle!=right.middle){return left.middle>right.middle?1:-1;}
    if(left.low!=right.low){return left.low>right.low?1:-1;}
    return 0;
}

static uint8_t cell(const uint8_t* board,int16_t row,int16_t column){return board[(uint16_t)row*BoardSize+(uint16_t)column];}

static uint8_t get_color(const uint8_t* board,int16_t row,int16_t column,int8_t dr,int8_t dc,bool next){
    int16_t target_row=(int16_t)(row+dr),target_column=(int16_t)(column+dc);
    if(target_row>=0&&target_row<BoardSize&&target_column>=0&&target_column<BoardSize){
        uint8_t value=cell(board,target_row,target_column);
        if(value!=0U){return (uint8_t)(3U-value);}
        return next?get_color(board,target_row,target_column,dr,dc,false):0U;
    }
    return next?3U:0U;
}

static DirectionScore direction_score(const uint8_t* board,uint8_t row,uint8_t column,int8_t dr,int8_t dc){
    uint8_t count=0U,player_count=0U,both=0U,player_both=0U;
    int8_t space=-1,player_space=-1;
    bool linked_player=false;
    uint8_t flag=get_color(board,row,column,dr,dc,true);
    if(flag==1U||flag==2U){
        for(uint8_t step=1U;step<=5U;step++){
            int16_t r=(int16_t)row+(int16_t)step*dr,c=(int16_t)column+(int16_t)step*dc;
            if(r>=0&&r<BoardSize&&c>=0&&c<BoardSize){
                uint8_t value=cell(board,r,c);
                if(flag==1U){
                    if(value==2U){count++;if(space==0){space=1;}}
                    else if(value==1U){if(space!=0){both++;}break;}
                    else if(space<0){space=0;}else{break;}
                }
                else{
                    if(value==2U){if(player_space!=0){player_both++;}break;}
                    if(value==1U){player_count++;linked_player=true;if(player_space==0){player_space=1;}}
                    else if(player_space<0){player_space=0;}else{break;}
                }
            }
            else{if(flag==1U){both++;}else{player_both++;}break;}
        }
    }
    else if(flag==3U){both++;}
    if(space==0){space=-1;}if(player_space==0){player_space=-1;}
    if(linked_player&&both==0U){both=1U;}

    linked_player=false;
    uint8_t reverse_flag=get_color(board,row,column,(int8_t)-dr,(int8_t)-dc,true);
    if(reverse_flag==1U||reverse_flag==2U){
        for(uint8_t step=1U;step<=5U;step++){
            int16_t r=(int16_t)row-(int16_t)step*dr,c=(int16_t)column-(int16_t)step*dc;
            if(r>=0&&r<BoardSize&&c>=0&&c<BoardSize){
                uint8_t value=cell(board,r,c);
                if(reverse_flag==1U){
                    if(value==2U){count++;if(space==0){space=1;}}
                    else if(value==1U){if(space!=0){both++;}break;}
                    else if(space<0){space=0;}else{break;}
                }
                else{
                    if(value==2U){if(player_space!=0){player_both++;}break;}
                    if(value==1U){player_count++;linked_player=true;if(player_space==0){player_space=1;}}
                    else if(player_space<0){player_space=0;}else{break;}
                }
            }
            else{if(reverse_flag==1U){both++;}else{player_both++;}break;}
        }
    }
    else if(reverse_flag==3U){both++;}
    if(linked_player&&both==1U){both=2U;}else if(linked_player&&both==0U){both=1U;}

    DirectionScore result={0U,0U,false,false};
    if(count>3U){result.ai=space>0?65U:10000U;result.ai_multiply=true;}
    else if(count==3U){
        if(both==0U){result.ai=space>0?65U:5000U;result.ai_multiply=true;}
        else if(both==1U){result.ai=space>0?60U:65U;result.ai_multiply=true;}
    }
    else if(count==2U){
        if(both==0U){result.ai=space>0?50U:55U;result.ai_multiply=true;}
        else if(both==1U){result.ai=20U;}
    }
    else if(count==1U){result.ai=both==0U?10U:(both==1U?5U:0U);}

    if(player_count>3U){result.player=player_space>0?65U:5000000U;result.player_multiply=true;}
    else if(player_count==3U){
        if(player_both==0U){result.player=player_space>0?65U:4000U;result.player_multiply=true;}
        else if(player_both==1U){result.player=player_space>0?55U:60U;result.player_multiply=true;}
    }
    else if(player_count==2U){
        if(player_both==0U){result.player=player_space>0?45U:50U;result.player_multiply=true;}
        else if(player_both==1U){result.player=15U;}
    }
    return result;
}

static Score96 point_score(const uint8_t* board,uint8_t row,uint8_t column){
    Score96 ai=score_from_u32(1U),player=score_from_u32(1U),sum=score_from_u32(0U);
    for(uint8_t direction=0U;direction<4U;direction++){
        DirectionScore value=direction_score(board,row,column,directions[direction][0],directions[direction][1]);
        if(value.ai_multiply){score_multiply(&ai,value.ai);}else{score_add_u32(&sum,value.ai);}
        if(value.player_multiply){score_multiply(&player,value.player);}else{score_add_u32(&sum,value.player);}
    }
    score_add(&sum,ai);score_add(&sum,player);score_add_u32(&sum,board_score[(uint16_t)row*BoardSize+column]);
    return sum;
}

}

bool GobangAi_has_five(const uint8_t* board,uint8_t row,uint8_t column,uint8_t stone){
    if(board==NULL||row>=BoardSize||column>=BoardSize||(stone!=1U&&stone!=2U)){return false;}
    for(uint8_t direction=0U;direction<4U;direction++){
        uint8_t count=1U;
        for(uint8_t step=1U;step<=5U;step++){
            int16_t r=(int16_t)row+(int16_t)directions[direction][0]*step,c=(int16_t)column+(int16_t)directions[direction][1]*step;
            if(r<0||r>=BoardSize||c<0||c>=BoardSize||cell(board,r,c)!=stone){break;}count++;
        }
        for(uint8_t step=1U;step<=5U;step++){
            int16_t r=(int16_t)row-(int16_t)directions[direction][0]*step,c=(int16_t)column-(int16_t)directions[direction][1]*step;
            if(r<0||r>=BoardSize||c<0||c>=BoardSize||cell(board,r,c)!=stone){break;}count++;
        }
        if(count>4U){return true;}
    }
    return false;
}

bool GobangAi_choose(uint8_t* board,GobangAiMove* move){
    if(board==NULL||move==NULL){return false;}
    Score96 best=score_from_u32(0U);bool found=false;
    for(uint8_t row=0U;row<BoardSize;row++){
        for(uint8_t column=0U;column<BoardSize;column++){
            uint16_t index=(uint16_t)row*BoardSize+column;
            if(board[index]!=0U){continue;}
            board[index]=2U;
            if(GobangAi_has_five(board,row,column,2U)){
                board[index]=0U;move->row=row;move->column=column;return true;
            }
            board[index]=0U;
            Score96 value=point_score(board,row,column);
            int8_t comparison=score_compare(value,best);
            if(!found||comparison>0){best=value;move->row=row;move->column=column;found=true;}
            else if(comparison==0&&Rng_get_range_int(1,60)>40){move->row=row;move->column=column;}
        }
    }
    return found;
}
