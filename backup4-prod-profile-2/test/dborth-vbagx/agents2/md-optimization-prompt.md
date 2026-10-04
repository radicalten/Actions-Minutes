https://www.reddit.com/r/ClaudeAI/comments/1w9i1iv/claude_pro_token_usage_has_increased_dramatically/
Sep 2026
Do you have a good prompt for an optimization round? 
u/accessiblebits
 
 
 Sure. The optimization round is basically an audit of your current .claude folder structure, paste this into claude code and let it report before it touches anything so you can assess what leg you are standing on.

Prompt:

Audit this project for token waste. Report first, change nothing yet

    CLAUDE.md size - anything that isn't a standing rule moves out to memory/

    What gets read EVERY session vs what could load on demand

    Subagent usage - list every place you would spawn one, then ask me first

    Files over ~500 lines that get fully read when one section would do

    When context passes ~60%, write a handoff .md and tell me to restart the chat

Then propose the folder split below and wait for my ok.

But honestly the prompt is the small half of the equation, you need a structure that is more like an index of things so your claude is not reading walls of text everytime you are trying to do something, an index is much more efficient for tokens than reading bunch of files. The big win for me was restructuring so the AI stops rereading everything. My folder looks like this now

project/

├── CLAUDE.md<- who I am, how I work, standing rules only

└── memory/

├── index.md<- READ FIRST every session, what exists + where

├── decisions.md<- calls and decisions made + why, each marked decided/proposed/dead

├── next-actions.md <- in-progress stuff

├── worklog.md<- the why behind changes

└── sessions/ <- one dated wrap-up per session

The rule that makes it cheap because is just an index first, one file on demand, never the whole pile. Plus vector search layer with Pinecone over memory/ for "did we ever discuss X" so it doesn't reread everything to answer. And subagents stay off unless I explicitly ask, that alone is what fixed my weekly limit. I see there is an option to disable agents as per the comment from u/YetAnotherHumanMale, disable that for sure. Also have in mind that the tools you connect on the web version still load on the local version too so all of that is going into the context, disable anything you are not using. Hope this helps :) 
