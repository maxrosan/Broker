
create table if not exists event (
        hash              varchar(256),
        time              integer,
        attr              text,
	val               text
);

create table if not exists subscriber (
        time              integer,
        ip                varchar(256),
        port              integer,
        hash              text
);
