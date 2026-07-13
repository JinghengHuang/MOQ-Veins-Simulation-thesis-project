---
name: research-developer
description: Implement functions and classes per user's request on a relatively large code base meant for research purposes
---

# Research Developer

This skill SHOULD run when a method or class are about to be implemented or changed in this project repository.

When invoked:

## Pre-editing checks
1. Compare the reasoning with the constraint documents under ./constraints folder, DO NOT drift from standards or requirements listed in the documents
2. Make sure your reasoning follow the research questions, do not go out of the bounds and go too deep into trivial issues. If you don't know what is the question, ask the user, continously take user input until the user says "that's all of the questions.". When asking, tell user to send this as their last input. 

# Setting up environments
For project to run certain scripts might be needed to start the background services. User may leave some document in the ./scripts directory, you can check and run them to start the service.

If there are none and you find something necessary to start the environment, suggest the user to add it to the folder.

## Report
Present findings as a brief .md document, including:
- What is done
- How does the change relate to the research questions
- What data supports your finding, present graph or table if possible (Only generate from actual test results, DO NOT make things up, report honestly even if the result are not as good as expected, and DO NOT consider it a bug unless the user tell you so)
- Suggested course of action, such as further changes or how to pitch this in the thesis, if there are different options, present them all and let user decide.

## Decision
If you have questions or need the user to make a decision, ask the user.

Always present a plan before editing.

Make a git commit per feature change/bug fix, and briefly document the 