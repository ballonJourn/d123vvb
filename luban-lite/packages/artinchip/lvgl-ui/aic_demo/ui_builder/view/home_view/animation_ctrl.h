#ifndef CARTOON_CTRL_H
#define CARTOON_CTRL_H

typedef enum {
    
    CARTOON_STATE_START   ,
    CARTOON_STATE_RUNNING ,
    
    CARTOON_STATE_MAX  ,
    
}cartoon_state_e;


typedef enum animation_theme{
    
    CARTOON_STYLE_1    ,
    CARTOON_STYLE_2    ,
    CARTOON_STYLE_3    ,

    CARTOON_STYLE_MAX  ,
    
}cartoon_theme_e;

void animation_init() ;
 
void animation_stop() ;
 
void animation_resume();

void animation_set_theme(cartoon_theme_e theme) ;
 
void animation_set_state(cartoon_state_e state) ;


#endif